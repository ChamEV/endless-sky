/* MapDetailPanel.cpp
Copyright (c) 2014 by Michael Zahniser

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.
*/

#include "MapDetailPanel.h"

#include "Color.h"
#include "Command.h"
#include "Font.h"
#include "FontSet.h"
#include "Format.h"
#include "GameData.h"
#include "Government.h"
#include "LineShader.h"
#include "MapOutfitterPanel.h"
#include "MapShipyardPanel.h"
#include "pi.h"
#include "Planet.h"
#include "PlayerInfo.h"
#include "PointerShader.h"
#include "Politics.h"
#include "Radar.h"
#include "RingShader.h"
#include "Screen.h"
#include "Ship.h"
#include "Sprite.h"
#include "SpriteSet.h"
#include "SpriteShader.h"
#include "StellarObject.h"
#include "System.h"
#include "Trade.h"
#include "UI.h"
#include "WrappedText.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>
#include <vector>

using namespace std;

namespace {
	// Convert the angle between two vectors into a sortable angle, i.e an angle
	// plus a length that is used as a tie-breaker.
	pair<double, double> SortAngle(const Point &reference, const Point &point)
	{
		// Rotate the given point by the reference amount.
		Point rotated(reference.Dot(point), reference.Cross(point));
		
		// This will be the tiebreaker value: the length, squared.
		double length = rotated.Dot(rotated);
		// Calculate the angle, but rotated 180 degrees so that the discontinuity
		// comes at the reference angle rather than directly opposite it.
		double angle = atan2(-rotated.Y(), -rotated.X());
		
		// Special case: collinear with the reference vector. If the point is
		// a longer vector than the reference, it's the very best angle.
		// Otherwise, it is the very worst angle. (Note: this also is applied if
		// the angle is opposite (angle == 0) but then it's a no-op.)
		if(!rotated.Y())
			angle = copysign(angle, rotated.X() - reference.Dot(reference));
		
		// Return the angle, plus the length as a tie-breaker.
		return make_pair(angle, length);
	}
}



MapDetailPanel::MapDetailPanel(PlayerInfo &player, const System *system)
	: MapPanel(player, system ? MapPanel::SHOW_REPUTATION : player.MapColoring(), system), compareSystem(player.GetSystem())
{
}



MapDetailPanel::MapDetailPanel(const MapPanel &panel)
	: MapPanel(panel), compareSystem(player.GetSystem())
{
	// Use whatever map coloring is specified in the PlayerInfo.
	commodity = player.MapColoring();
}



void MapDetailPanel::Step()
{
	MapPanel::Step();
	if(!player.GetPlanet())
		DoHelp("map");
}



void MapDetailPanel::Draw()
{
	MapPanel::Draw();
	
	DrawInfo();
	DrawOrbits();
	DrawKey();
	DrawTradePlan();
}



// Only override the ones you need; the default action is to return false.
bool MapDetailPanel::KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool isNewPress)
{
	if((key == SDLK_TAB || command.Has(Command::JUMP)) && player.Flagship())
	{
		// Clear the selected planet, if any.
		selectedPlanet = nullptr;
		// Toggle to the next link connected to the "source" system. If the
		// shift key is down, the source is the end of the travel plan; otherwise
		// it is one step before the end.
		vector<const System *> &plan = player.TravelPlan();
		const System *source = plan.empty() ? player.GetSystem() : plan.front();
		const System *next = nullptr;
		Point previousUnit = Point(0., -1.);
		if(!plan.empty() && !(mod & KMOD_SHIFT))
		{
			previousUnit = plan.front()->Position();
			plan.erase(plan.begin());
			next = source;
			source = plan.empty() ? player.GetSystem() : plan.front();
			previousUnit = (previousUnit - source->Position()).Unit();
		}
		Point here = source->Position();
		const System *original = next;
		
		// Depending on whether the flagship has a jump drive, the possible links
		// we can travel along are different:
		bool hasJumpDrive = player.Flagship()->Attributes().Get("jump drive");
		const set<const System *> &links = hasJumpDrive ? source->Neighbors() : source->Links();
		
		// For each link we can travel from this system, check whether the link
		// is closer to the current angle (while still being larger) than any
		// link we have seen so far.
		auto bestAngle = make_pair(4., 0.);
		for(const System *it : links)
		{
			// Skip the currently selected link, if any. Also skip links to
			// systems the player has not seen, and skip hyperspace links if the
			// player has not visited either end of them.
			if(it == original)
				continue;
			if(!player.HasSeen(it))
				continue;
			if(!(hasJumpDrive || player.HasVisited(it) || player.HasVisited(source)))
				continue;
			
			// Generate a sortable angle with vector length as a tiebreaker.
			// Otherwise if two systems are in exactly the same direction it is
			// not well defined which one comes first.
			auto angle = SortAngle(previousUnit, it->Position() - here);
			if(angle < bestAngle)
			{
				next = it;
				bestAngle = angle;
			}
		}
		if(next)
		{
			plan.insert(plan.begin(), next);
			Select(next);
		}
	}
	else if((key == SDLK_DELETE || key == SDLK_BACKSPACE) && player.HasTravelPlan())
	{
		vector<const System *> &plan = player.TravelPlan();
		plan.erase(plan.begin());
		Select(plan.empty() ? player.GetSystem() : plan.front());
	}
	else if(key == SDLK_DOWN)
	{
		if(commodity < 0 || commodity == 9)
			SetCommodity(0);
		else
			SetCommodity(commodity + 1);
	}
	else if(key == SDLK_UP)
	{
		if(commodity <= 0)
			SetCommodity(9);
		else
			SetCommodity(commodity - 1);
	}
	else
		return MapPanel::KeyDown(key, mod, command, isNewPress);
	
	return true;
}



bool MapDetailPanel::Click(int x, int y, int clicks)
{
	if(x < Screen::Left() + (wideCommodity ? 190 : 160) && y >= tradeY && y < tradeY + 200)
	{
		SetCommodity((y - tradeY) / 20);
		return true;
	}
	if(x < Screen::Left() + 160)
	{
		if(y < governmentY)
			SetCommodity(SHOW_REPUTATION);
		else if(y >= governmentY && y < governmentY + 20)
			SetCommodity(SHOW_GOVERNMENT);
		else
		{
			for(const auto &it : planetY)
				if(y >= it.second && y < it.second + 110)
				{
					selectedPlanet = it.first;
					if(y >= it.second + 30 && y < it.second + 110)
					{
						// Figure out what row of the planet info was clicked.
						int row = (y - (it.second + 30)) / 20;
						static const int SHOW[4] = {
							SHOW_REPUTATION, SHOW_SHIPYARD, SHOW_OUTFITTER, SHOW_VISITED};
						SetCommodity(SHOW[row]);
						
						if(clicks > 1 && SHOW[row] == SHOW_SHIPYARD)
						{
							GetUI()->Pop(this);
							GetUI()->Push(new MapShipyardPanel(*this, true));
						}
						if(clicks > 1 && SHOW[row] == SHOW_OUTFITTER)
						{
							GetUI()->Pop(this);
							GetUI()->Push(new MapOutfitterPanel(*this, true));
						}
					}
					return true;
				}
		}
	}
	else if(x >= Screen::Right() - 240 && y <= Screen::Top() + 270)
	{
		Point click = Point(x, y);
		selectedPlanet = nullptr;
		double distance = numeric_limits<double>::infinity();
		for(const auto &it : planets)
		{
			double d = click.Distance(it.second);
			if(d < distance)
			{
				distance = d;
				selectedPlanet = it.first;
			}
		}
		if(selectedPlanet && player.Flagship())
			player.SetTravelDestination(selectedPlanet);
		
		return true;
	}
	else if(y >= Screen::Bottom() - 40 && x >= Screen::Right() - 335 && x < Screen::Right() - 265)
	{
		// The user clicked the "done" button.
		return DoKey(SDLK_d);
	}
	else if(y >= Screen::Bottom() - 40 && x >= Screen::Right() - 415 && x < Screen::Right() - 345)
	{
		// The user clicked the "missions" button.
		return DoKey(SDLK_PAGEDOWN);
	}
	
	MapPanel::Click(x, y, clicks);
	if(selectedPlanet && !selectedPlanet->IsInSystem(selectedSystem))
		selectedPlanet = nullptr;
	return true;
}



void MapDetailPanel::Select(const System *system)
{
	// We handle ctrl-click select entirely here.
	if(SDL_GetModState() & KMOD_CTRL)
	{
		if(system && player.Flagship() && system->IsInhabited(player.Flagship()))
			compareSystem = system;
		else
			compareSystem = player.GetSystem();
	}
	else
		MapPanel::Select(system);
	
	tradeRoute.clear();
	
	// Check compareSystem and selectedSystem
	if(!compareSystem || !selectedSystem || compareSystem == selectedSystem)
		return;
	
	// Create a map around compareSystem and save a route to selectedSystem
	DistanceMap localRoute(player,compareSystem);
	const System *curSystem = selectedSystem;
	vector<const System *> &plan = player.TravelPlan();
	// Temporarily add player system to travel plan.
	plan.push_back(player.GetSystem());
	
	while(curSystem != compareSystem)
	{
		const System *prevSystem = curSystem;
		curSystem = localRoute.Route(curSystem);
		if(!curSystem)
		{
			// no route to system
			tradeRoute.clear();
			break;
		}
		
		// Make sure the new trade link is not in the travel plan so we don't draw over it.
		bool addLink = true;
		for (size_t j = 0; j < plan.size(); ++j)
		{
			if((curSystem == plan[j] && ((j - 1 > 0 && prevSystem == plan[j-1])
							|| (j + 1 < plan.size() && prevSystem == plan[j+1])))
							|| (prevSystem == plan[j] && ((j - 1 > 0 && curSystem == plan[j-1])
							|| (j + 1 < plan.size() && curSystem == plan[j+1]))))
			{
				addLink = false;
				break;
			}
		}
		if(addLink)
		{
			tradeRoute.push_back(prevSystem);
			tradeRoute.push_back(curSystem);
		}
	}
	// Remove temporarily added player system from travel plan
	plan.pop_back();
}



// Draw the legend, correlating between a system's color and the value of the
// selected "commodity," which may be reputation level, outfitter size, etc.
void MapDetailPanel::DrawKey()
{
	const Color &dim = *GameData::Colors().Get("dim");
	const Color &medium = *GameData::Colors().Get("medium");
	const Font &font = FontSet::Get(14);
	
	Point pos = Screen::TopRight() + Point(-110., 310.);
	Point headerOff(-5., -.5 * font.Height());
	Point textOff(10., -.5 * font.Height());
	
	static const string HEADER[] = {
		"Trade prices:",
		"Ships for sale:",
		"Outfits for sale:",
		"You have visited:",
		"", // Special should never be active in this mode.
		"Government:",
		"System:"
	};
	const string &header = HEADER[-min(0, max(-6, commodity))];
	font.Draw(header, pos + headerOff, medium);
	pos.Y() += 20.;
	
	if(commodity >= 0)
	{
		// Each system is colored by the selected commodity's price. Draw
		// four distinct colors and the price each color represents.
		const vector<Trade::Commodity> &commodities = GameData::Commodities();
		const auto &range = commodities[commodity];
		if(static_cast<unsigned>(commodity) >= commodities.size())
			return;
		
		for(int i = 0; i <= 3; ++i)
		{
			RingShader::Draw(pos, OUTER, INNER, MapColor(i * (2. / 3.) - 1.));
			int price = range.low + ((range.high - range.low) * i) / 3;
			font.Draw(to_string(price), pos + textOff, dim);
			pos.Y() += 20.;
		}
	}
	else if(commodity >= SHOW_OUTFITTER)
	{
		// Each system is colored by the number of outfits for sale.
		static const string LABEL[2][4] = {
			{"None", "1", "5", "10+"},
			{"None", "1", "30", "60+"}};
		static const double VALUE[4] = {-1., 0., .5, 1.};
		
		for(int i = 0; i < 4; ++i)
		{
			RingShader::Draw(pos, OUTER, INNER, MapColor(VALUE[i]));
			font.Draw(LABEL[commodity == SHOW_OUTFITTER][i], pos + textOff, dim);
			pos.Y() += 20.;
		}
	}
	else if(commodity == SHOW_VISITED)
	{
		static const string LABEL[3] = {
			"All planets",
			"Some",
			"None"
		};
		for(int i = 0; i < 3; ++i)
		{
			RingShader::Draw(pos, OUTER, INNER, MapColor(1 - i));
			font.Draw(LABEL[i], pos + textOff, dim);
			pos.Y() += 20.;
		}
	}
	else if(commodity == SHOW_GOVERNMENT)
	{
		// Each system is colored by the government of the system. Only the
		// four largest visible governments are labeled in the legend.
		vector<pair<double, const Government *>> distances;
		for(const auto &it : closeGovernments)
			distances.emplace_back(it.second, it.first);
		sort(distances.begin(), distances.end());
		for(unsigned i = 0; i < 4 && i < distances.size(); ++i)
		{
			RingShader::Draw(pos, OUTER, INNER, GovernmentColor(distances[i].second));
			font.Draw(distances[i].second->GetName(), pos + textOff, dim);
			pos.Y() += 20.;
		}
	}
	else if(commodity == SHOW_REPUTATION)
	{
		// Each system is colored in accordance with the player's reputation
		// with its owning government. The specific colors associated with a
		// given reputation (0.1, 100, and 10000) are shown for each sign.
		RingShader::Draw(pos, OUTER, INNER, ReputationColor(1e-1, true, false));
		RingShader::Draw(pos + Point(12., 0.), OUTER, INNER, ReputationColor(1e2, true, false));
		RingShader::Draw(pos + Point(24., 0.), OUTER, INNER, ReputationColor(1e4, true, false));
		font.Draw("Friendly", pos + textOff + Point(24., 0.), dim);
		pos.Y() += 20.;
		
		RingShader::Draw(pos, OUTER, INNER, ReputationColor(-1e-1, false, false));
		RingShader::Draw(pos + Point(12., 0.), OUTER, INNER, ReputationColor(-1e2, false, false));
		RingShader::Draw(pos + Point(24., 0.), OUTER, INNER, ReputationColor(-1e4, false, false));
		font.Draw("Hostile", pos + textOff + Point(24., 0.), dim);
		pos.Y() += 20.;
		
		RingShader::Draw(pos, OUTER, INNER, ReputationColor(0., false, false));
		font.Draw("Restricted", pos + textOff, dim);
		pos.Y() += 20.;
		
		RingShader::Draw(pos, OUTER, INNER, ReputationColor(0., false, true));
		font.Draw("Dominated", pos + textOff, dim);
		pos.Y() += 20.;
	}
	
	RingShader::Draw(pos, OUTER, INNER, UninhabitedColor());
	font.Draw("Uninhabited", pos + textOff, dim);
	pos.Y() += 20.;
	
	RingShader::Draw(pos, OUTER, INNER, UnexploredColor());
	font.Draw("Unexplored", pos + textOff, dim);
}



// Draw the various information displays: system name & government, planetary
// details, trade prices, and details about the selected object.
void MapDetailPanel::DrawInfo()
{
	const Color &faint = *GameData::Colors().Get("faint");
	const Color &dim = *GameData::Colors().Get("dim");
	const Color &medium = *GameData::Colors().Get("medium");
	
	Point uiPoint(Screen::Left() + 100., Screen::Top() + 45.);
	
	// System sprite goes from 0 to 90.
	const Sprite *systemSprite = SpriteSet::Get("ui/map system");
	SpriteShader::Draw(systemSprite, uiPoint);
	
	const Font &font = FontSet::Get(14);
	string systemName = player.KnowsName(selectedSystem) ?
		selectedSystem->Name() : "Unexplored System";
	font.Draw(systemName, uiPoint + Point(-90., -7.), medium);
	
	governmentY = uiPoint.Y() + 10.;
	string gov = player.HasVisited(selectedSystem) ?
		selectedSystem->GetGovernment()->GetName() : "Unknown Government";
	font.Draw(gov, uiPoint + Point(-90., 13.), (commodity == SHOW_GOVERNMENT) ? medium : dim);
	if(commodity == SHOW_GOVERNMENT)
		PointerShader::Draw(uiPoint + Point(-90., 20.), Point(1., 0.),
			10.f, 10.f, 0.f, medium);
	
	uiPoint.Y() += 115.;
	
	bool hasVisitedSelectedSystem = player.HasVisited(selectedSystem);
	bool hasVisitedCompareSystem = player.HasVisited(compareSystem);
	
	planetY.clear();
	// Draw the basic information for visitable planets in this system.
	if(hasVisitedSelectedSystem)
	{
		set<const Planet *> shown;
		const Sprite *planetSprite = SpriteSet::Get("ui/map planet");
		for(const StellarObject &object : selectedSystem->Objects())
			if(object.GetPlanet())
			{
				// The same "planet" may appear multiple times in one system,
				// providing multiple landing and departure points (e.g. ringworlds).
				const Planet *planet = object.GetPlanet();
				if(planet->IsWormhole() || !planet->IsAccessible(player.Flagship()) || shown.count(planet))
					continue;
				shown.insert(planet);
				
				SpriteShader::Draw(planetSprite, uiPoint);
				planetY[planet] = uiPoint.Y() - 60;
			
				font.Draw(object.Name(),
					uiPoint + Point(-70., -52.),
					planet == selectedPlanet ? medium : dim);
				
				bool hasSpaceport = planet->HasSpaceport();
				string reputationLabel = !hasSpaceport ? "No Spaceport" :
					GameData::GetPolitics().HasDominated(planet) ? "Dominated" :
					planet->GetGovernment()->IsEnemy() ? "Hostile" :
					planet->CanLand() ? "Friendly" : "Restricted";
				font.Draw(reputationLabel,
					uiPoint + Point(-60., -32.),
					hasSpaceport ? medium : faint);
				if(commodity == SHOW_REPUTATION)
					PointerShader::Draw(uiPoint + Point(-60., -25.), Point(1., 0.),
						10.f, 10.f, 0.f, medium);
				
				font.Draw("Shipyard",
					uiPoint + Point(-60., -12.),
					planet->HasShipyard() ? medium : faint);
				if(commodity == SHOW_SHIPYARD)
					PointerShader::Draw(uiPoint + Point(-60., -5.), Point(1., 0.),
						10.f, 10.f, 0.f, medium);
				
				font.Draw("Outfitter",
					uiPoint + Point(-60., 8.),
					planet->HasOutfitter() ? medium : faint);
				if(commodity == SHOW_OUTFITTER)
					PointerShader::Draw(uiPoint + Point(-60., 15.), Point(1., 0.),
						10.f, 10.f, 0.f, medium);
				
				bool hasVisited = player.HasVisited(planet);
				font.Draw(hasVisited ? "(has been visited)" : "(not yet visited)",
					uiPoint + Point(-70., 28.),
					dim);
				if(commodity == SHOW_VISITED)
					PointerShader::Draw(uiPoint + Point(-70., 35.), Point(1., 0.),
						10.f, 10.f, 0.f, medium);
				
				uiPoint.Y() += 130.;
			}
	}
	
	uiPoint.Y() += 45.;
	tradeY = uiPoint.Y() - 95.;
	
	bool compare = compareSystem && compareSystem->IsInhabited(player.Flagship()) && compareSystem != selectedSystem
						&& hasVisitedCompareSystem && hasVisitedSelectedSystem && selectedSystem->IsInhabited(player.Flagship());
	
	// Trade sprite goes from 310 to 540.
	// Determine trade type and draw correct background UI sprite.
	if (compare)
	{
		const Sprite *tradeSpriteExtended = SpriteSet::Get("ui/map trade extended");
		SpriteShader::Draw(tradeSpriteExtended, uiPoint);
		// Tells click area to expand.
		wideCommodity = true;
	}
	else
	{
		const Sprite *tradeSprite = SpriteSet::Get("ui/map trade");
		SpriteShader::Draw(tradeSprite, uiPoint);
		wideCommodity = false;
	}
	
	uiPoint.X() -= 90.;
	uiPoint.Y() -= 97.;
	for(const Trade::Commodity &commodity : GameData::Commodities())
	{
		bool isSelected = false;
		if(static_cast<unsigned>(this->commodity) < GameData::Commodities().size())
			isSelected = (&commodity == &GameData::Commodities()[this->commodity]);
		const Color &color = isSelected ? medium : dim;
		
		font.Draw(commodity.name, uiPoint, color);
		
		string price;
		string priceDifference;
		if(hasVisitedSelectedSystem && selectedSystem->IsInhabited(player.Flagship()))
		{
			int value = selectedSystem->Trade(commodity.name);
			int localValue = (compareSystem ? compareSystem->Trade(commodity.name) : 0);
			// Don't "compare" prices if the current system is uninhabited and
			// thus has no prices to compare to.
			if(!value)
				price = "----";
			else if(!compare || compareSystem == selectedSystem || !localValue)
				price = to_string(value);
			else
			{
				price += to_string(value);
				
				int difference = value - localValue;
				if(difference > 0)
					priceDifference += '+';
				priceDifference += to_string(difference);
				
				Color red = Color(.75,0.,0.,0.);
				Color green = Color(0.,.75,0.,0.);
				Point posDifference = uiPoint + Point(140.,0.);
				float blend = max(min(difference,500),-500) / 500.;
				
				font.Draw("(", posDifference, color);
				font.Draw(priceDifference, posDifference + Point(5,0), dim.Blend(blend > 0 ? green : red, sqrt(abs(blend))));
				font.Draw(")", posDifference + Point(3 + font.Width(priceDifference),0), color);
			}
		}
		else
			price = (hasVisitedSelectedSystem ? "n/a" : "?");
		
		Point pos = uiPoint + Point(136. - font.Width(price), 0.);
		font.Draw(price, pos, color);
		
		if(isSelected)
			PointerShader::Draw(uiPoint + Point(0., 7.), Point(1., 0.), 10.f, 10.f, 0.f, color);
		
		uiPoint.Y() += 20.;
	}
	
	if(selectedPlanet && !selectedPlanet->Description().empty()
			&& player.HasVisited(selectedPlanet) && !selectedPlanet->IsWormhole())
	{
		static const int X_OFFSET = 240;
		static const int WIDTH = 500;
		const Sprite *panelSprite = SpriteSet::Get("ui/description panel");
		Point pos(Screen::Right() - X_OFFSET - .5f * panelSprite->Width(),
			Screen::Top() + .5f * panelSprite->Height());
		SpriteShader::Draw(panelSprite, pos);
		
		WrappedText text(font);
		text.SetAlignment(WrappedText::JUSTIFIED);
		text.SetWrapWidth(WIDTH - 20);
		text.Wrap(selectedPlanet->Description());
		text.Draw(Point(Screen::Right() - X_OFFSET - WIDTH, Screen::Top() + 20), medium);
	}
	
	DrawButtons("is ports");
}



// Draw the planet orbits in the currently selected system, on the current day.
void MapDetailPanel::DrawOrbits()
{
	const Sprite *orbitSprite = SpriteSet::Get("ui/orbits and key");
	SpriteShader::Draw(orbitSprite, Screen::TopRight() + .5 * Point(-orbitSprite->Width(), orbitSprite->Height()));
	Point orbitCenter = Screen::TopRight() + Point(-120., 160.);
	
	if(!selectedSystem || !player.HasVisited(selectedSystem))
		return;
	
	const Font &font = FontSet::Get(14);
	
	// Figure out what the largest orbit in this system is.
	double maxDistance = 0.;
	for(const StellarObject &object : selectedSystem->Objects())
		maxDistance = max(maxDistance, object.Position().Length() + object.Radius());
	
	// 2400 -> 120.
	double scale = .03;
	maxDistance *= scale;
	
	if(maxDistance > 115.)
		scale *= 115. / maxDistance;
	
	// Draw the orbits.
	static const Color habitColor[7] = {
		Color(.4, .2, .2, 1.),
		Color(.3, .3, 0., 1.),
		Color(0., .4, 0., 1.),
		Color(0., .3, .4, 1.),
		Color(.1, .2, .5, 1.),
		Color(.2, .2, .2, 1.),
		Color(1., 1., 1., 1.)
	};
	for(const StellarObject &object : selectedSystem->Objects())
	{
		if(object.Radius() <= 0.)
			continue;
		
		Point parentPos;
		int habit = 5;
		if(object.Parent() >= 0)
			parentPos = selectedSystem->Objects()[object.Parent()].Position();
		else
		{
			double warmth = object.Distance() / selectedSystem->HabitableZone();
			habit = (warmth > .5) + (warmth > .8) + (warmth > 1.2) + (warmth > 2.0);
		}
		
		double radius = object.Distance() * scale;
		RingShader::Draw(orbitCenter + parentPos * scale,
			radius + .7, radius - .7,
			habitColor[habit]);
	}
	
	// Draw the planets themselves.
	planets.clear();
	for(const StellarObject &object : selectedSystem->Objects())
	{
		if(object.Radius() <= 0.)
			continue;
		
		Point pos = orbitCenter + object.Position() * scale;
		if(object.GetPlanet() && object.GetPlanet()->IsAccessible(player.Flagship()))
			planets[object.GetPlanet()] = pos;
		
		const float *rgb = Radar::GetColor(object.RadarType(player.Flagship())).Get();
		// Darken and saturate the color, and make it opaque.
		Color color(max(0.f, rgb[0] * 1.2f - .2f), max(0.f, rgb[1] * 1.2f - .2f), max(0.f, rgb[2] * 1.2f - .2f), 1.f);
		RingShader::Draw(pos, object.Radius() * scale + 1., 0.f, color);
	}
	
	// Draw the selection ring on top of everything else.
	for(const StellarObject &object : selectedSystem->Objects())
		if(selectedPlanet && object.GetPlanet() == selectedPlanet)
			RingShader::Draw(orbitCenter + object.Position() * scale,
				object.Radius() * scale + 5., object.Radius() * scale + 4.,
				habitColor[6]);
	
	// Draw the name of the selected planet.
	const string &name = selectedPlanet ? selectedPlanet->Name() : selectedSystem->Name();
	Point namePos(Screen::Right() - .5 * font.Width(name) - 100., Screen::Top() + 7.);
	font.Draw(name, namePos, *GameData::Colors().Get("medium"));
}



// Draw the quickest trade route when comparing commodity prices.
void MapDetailPanel::DrawTradePlan()
{
	// Cycle through every pair of systems to draw the route.  Never draws over travel plan.
	for (size_t pos = 0; pos + 1 < tradeRoute.size(); pos += 2)
	{
		const System *previous = tradeRoute[pos];
		const System *next = tradeRoute[pos+1];
		const Color lightBlue = Color(0.,.5,.8,0.);
		
		Point from = Zoom() * (next->Position() + center);
		Point to = Zoom() * (previous->Position() + center);
		Point unit = (from - to).Unit() * LINK_OFFSET;
		LineShader::Draw(from - unit, to + unit, 3.f, lightBlue);
	}
	// Draw the ring around compareSystem
	if (compareSystem && compareSystem != player.GetSystem())
	{
		Color tradeRingColor(.4f, .4f, 0.f, 0.f);
		RingShader::Draw(Zoom() * (compareSystem->Position() + center), 14.f, 12.f, tradeRingColor);
	}
}



// Set the commodity coloring, and update the player info as well.
void MapDetailPanel::SetCommodity(int index)
{
	commodity = index;
	player.SetMapColoring(commodity);
}
