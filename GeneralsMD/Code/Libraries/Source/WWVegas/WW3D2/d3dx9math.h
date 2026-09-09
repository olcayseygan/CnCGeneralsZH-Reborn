/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// The D3DX vector and matrix types for the native Direct3D 9 renderer.
// RENDERER-ROADMAP.md phase 1.
//
// d3dx8math.h cannot come along, because it includes d3dx8.h, which includes d3d8.h,
// and the whole point of the phase is that d3d8.h is gone.  The types themselves owe
// nothing to either header: they are floats with constructors.  So they are declared
// here, laid out exactly as the D3DX ones are, and the eight functions the engine calls
// that are not inline come out of d3dx9_43.dll beside the texture and shader ones.
//
// Binding rather than reimplementing is deliberate.  D3DXVec4Transform and D3DXVec4Dot
// reach GameLogic through BezierSegment, which DumbProjectileBehavior steers a shell
// with, so their arithmetic is part of the network and replay CRC.  A hand-written 4x4
// inverse or transform would be a rounding difference nobody could see until a replay
// diverged.  D3DXVec4Dot and D3DXMatrixIdentity are the two the DirectX SDK inlined, so
// those two are written out here, matching d3dx8math.inl term for term.

#ifndef D3DX9MATH_H
#define D3DX9MATH_H

#include <d3d9.h>

#define D3DX_PI	((FLOAT)3.141592654f)

struct D3DXVECTOR3 : public D3DVECTOR
{
public:
	D3DXVECTOR3() {}
	D3DXVECTOR3(FLOAT x_value, FLOAT y_value, FLOAT z_value)
	{
		x = x_value;
		y = y_value;
		z = z_value;
	}

	operator FLOAT * () { return &x; }
	operator const FLOAT * () const { return &x; }
};

struct D3DXVECTOR4
{
public:
	D3DXVECTOR4() {}
	D3DXVECTOR4(FLOAT x_value, FLOAT y_value, FLOAT z_value, FLOAT w_value)
		: x(x_value), y(y_value), z(z_value), w(w_value) {}

	operator FLOAT * () { return &x; }
	operator const FLOAT * () const { return &x; }

	FLOAT x;
	FLOAT y;
	FLOAT z;
	FLOAT w;
};

struct D3DXMATRIX : public D3DMATRIX
{
public:
	D3DXMATRIX() {}
	D3DXMATRIX(FLOAT m11, FLOAT m12, FLOAT m13, FLOAT m14,
	           FLOAT m21, FLOAT m22, FLOAT m23, FLOAT m24,
	           FLOAT m31, FLOAT m32, FLOAT m33, FLOAT m34,
	           FLOAT m41, FLOAT m42, FLOAT m43, FLOAT m44)
	{
		_11 = m11; _12 = m12; _13 = m13; _14 = m14;
		_21 = m21; _22 = m22; _23 = m23; _24 = m24;
		_31 = m31; _32 = m32; _33 = m33; _34 = m34;
		_41 = m41; _42 = m42; _43 = m43; _44 = m44;
	}

	operator FLOAT * () { return &_11; }
	operator const FLOAT * () const { return &_11; }

	FLOAT & operator()(UINT row, UINT column) { return m[row][column]; }
	FLOAT operator()(UINT row, UINT column) const { return m[row][column]; }

	// Defined below, once D3DXMatrixMultiply has been declared.
	D3DXMATRIX operator*(const D3DXMATRIX & right) const;
	D3DXMATRIX & operator*=(const D3DXMATRIX & right);
};

typedef HRESULT (WINAPI * D3DXMatrixInverseFunction)(D3DXMATRIX * out, FLOAT * determinant,
	const D3DXMATRIX * matrix);

typedef D3DXMATRIX * (WINAPI * D3DXMatrixBinaryFunction)(D3DXMATRIX * out,
	const D3DXMATRIX * left, const D3DXMATRIX * right);

typedef D3DXMATRIX * (WINAPI * D3DXMatrixUnaryFunction)(D3DXMATRIX * out,
	const D3DXMATRIX * matrix);

typedef D3DXMATRIX * (WINAPI * D3DXMatrixTripleFunction)(D3DXMATRIX * out,
	FLOAT first, FLOAT second, FLOAT third);

typedef D3DXMATRIX * (WINAPI * D3DXMatrixAngleFunction)(D3DXMATRIX * out, FLOAT angle);

typedef D3DXVECTOR4 * (WINAPI * D3DXVec4TransformFunction)(D3DXVECTOR4 * out,
	const D3DXVECTOR4 * vector, const D3DXMATRIX * matrix);

typedef D3DXVECTOR4 * (WINAPI * D3DXVec3TransformFunction)(D3DXVECTOR4 * out,
	const D3DXVECTOR3 * vector, const D3DXMATRIX * matrix);

// D3DXMatrixInverse returns null when the matrix is singular, so its result is not
// interchangeable with the others and it keeps its own signature.
extern D3DXMatrixInverseFunction	D3DXMatrixInverse;
extern D3DXMatrixBinaryFunction		D3DXMatrixMultiply;
extern D3DXMatrixUnaryFunction		D3DXMatrixTranspose;
extern D3DXMatrixTripleFunction		D3DXMatrixScaling;
extern D3DXMatrixTripleFunction		D3DXMatrixTranslation;
extern D3DXMatrixAngleFunction		D3DXMatrixRotationZ;
extern D3DXVec4TransformFunction	D3DXVec4Transform;
extern D3DXVec3TransformFunction	D3DXVec3Transform;

// These eight are bound by Bind_D3DX9_Runtime in d3dx9runtime.h, along with the texture
// and shader entry points: one place decides whether D3DX9 is present, and one answer
// covers all of it.  Every pointer is null until it succeeds.

// The D3DX matrix product, which is D3DXMatrixMultiply and nothing else: writing the
// sixty-four multiplies out here instead would put a second, differently rounded matrix
// product in the same renderer.
inline D3DXMATRIX D3DXMATRIX::operator*(const D3DXMATRIX & right) const
{
	D3DXMATRIX product;
	D3DXMatrixMultiply(&product, this, &right);
	return product;
}

inline D3DXMATRIX & D3DXMATRIX::operator*=(const D3DXMATRIX & right)
{
	D3DXMatrixMultiply(this, this, &right);
	return *this;
}

// The two the DirectX SDK inlined, copied from d3dx8math.inl so the arithmetic that
// reaches a replay CRC is the arithmetic that always reached it.
inline FLOAT D3DXVec4Dot(const D3DXVECTOR4 * left, const D3DXVECTOR4 * right)
{
	return left->x * right->x + left->y * right->y + left->z * right->z + left->w * right->w;
}

inline D3DXMATRIX * D3DXMatrixIdentity(D3DXMATRIX * out)
{
	out->m[0][1] = out->m[0][2] = out->m[0][3] =
	out->m[1][0] = out->m[1][2] = out->m[1][3] =
	out->m[2][0] = out->m[2][1] = out->m[2][3] =
	out->m[3][0] = out->m[3][1] = out->m[3][2] = 0.0f;

	out->m[0][0] = out->m[1][1] = out->m[2][2] = out->m[3][3] = 1.0f;
	return out;
}

#endif // D3DX9MATH_H
