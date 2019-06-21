/* Color.cpp
Copyright (c) 2014 by Michael Zahniser

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.
*/

#include "Color.h"



// Greyscale color constructor.
Color::Color(float i, float a)
	: color{i, i, i, a}
{
}



// Full color constructor.
Color::Color(float r, float g, float b, float a)
	: color{r, g, b, a}
{
}



// Set all four color components to the given values.
void Color::Load(double r, double g, double b, double a)
{
	color[0] = static_cast<float>(r);
	color[1] = static_cast<float>(g);
	color[2] = static_cast<float>(b);
	color[3] = static_cast<float>(a);
}



// Get a float vector representing this color, for use by OpenGL.
const float *Color::Get() const
{
	return color;
}



// Get an opaque version of this color.
Color Color::Opaque() const
{
	Color opaque = *this;
	opaque.color[3] = 1.f;
	return opaque;
}



// Assuming this color is opaque, get a transparent version of it.
Color Color::Transparent(float alpha) const
{
	Color result;
	for(int i = 0; i < 3; ++i)
		result.color[i] = color[i] * alpha;
	result.color[3] = alpha;
	
	return result;
}



// Assuming this color is opaque, get an additive version of it.
Color Color::Additive(float alpha) const
{
	Color result = Transparent(alpha);
	result.color[3] = 0.f;
	
	return result;
}



// Blend two colors with blendRatio between 0 and 1 determining how much blendColor is used.
Color Color::Blend(const Color &blendColor, float blendRatio) const
{
	float alt = 1 - blendRatio;
	return Color(alt * color[0] + blendRatio * blendColor.color[0],alt * color[1] + blendRatio * blendColor.color[1],
				alt * color[2] + blendRatio * blendColor.color[2],alt * color[3] + blendRatio * blendColor.color[3]);
}
