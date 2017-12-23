#pragma once

#ifndef __XSWAPBYTES_H__
#define __XSWAPBYTES_H__

#define I386
#if defined X86_64
	// 如果是64位处理器，用如下字节转换
	#if defined BYTE_ORDER_BIG_ENDIAN
		#define SwapByte16( x )	( x )
		#define SwapByte32( x )	( x )
		#define SwapByte64( x )	( x )
	#elif defined __GNUC__
		inline unsigned short SwapByte16(unsigned short x)
		{
			register unsigned short v;
			__asm__("xchg %b0, %h0" : "=q"(v) : "0"(x));
			return v;
		}
		inline unsigned int SwapByte32(unsigned int x)
		{
			register unsigned int v;
			__asm__("bswapl %0" : "=r"(v) : "0"(x));
			return v;
		}
		inline unsigned long SwapByte64(unsigned long x)
		{
			register unsigned long v;
			__asm__("bswapq %0":"=r"(v) : "0"(x));
			return v;
		}
	#elif defined WIN32
		inline unsigned short SwapByte16(unsigned short vData)
		{
			__asm ror vData, 8
			return vData;
		}
		inline unsigned int SwapByte32(unsigned int vData)
		{
			__asm mov eax, vData
			__asm bswap eax
			__asm mov vData, eax
			return vData;
		}
		inline unsigned long long SwapByte64(unsigned long long vData)
		{
			__asm mov rax, vData
			__asm bswap rax
			__asm mov vData, rax
			return vData;
		}
	#endif
#elif defined I386
	// 如果是32位处理器，用如下字节转换
	#if defined BYTE_ORDER_BIG_ENDIAN
		#define SwapByte16( x )	( x )
		#define SwapByte32( x ) ( x )
		#define SwapByte64( x )	( x )
	#elif defined __GNUC__
		inline unsigned short SwapByte16(unsigned short vData)
		{
			unsigned char tHIChar = (vData & 0xFF00) >> 8;
			unsigned char tLOChar = vData & 0x00FF;
			return ((tLOChar << 8) | tHIChar);
		}
		inline unsigned long SwapByte32(unsigned long vData)
		{
			unsigned char tChar1 = (vData & 0xFF000000) >> 24;
			unsigned char tChar2 = (vData & 0x00FF0000) >> 16;
			unsigned char tChar3 = (vData & 0x0000FF00) >> 8;
			unsigned char tChar4 = vData & 0x000000FF;
			return (tChar4 << 24) | (tChar3 << 16) | (tChar2 << 8) | tChar1;
		}
		inline unsigned long long SwapByte64(unsigned long long vData)
		{
			union
			{
				unsigned long long __ll;
				unsigned int __l[2];
			} tSrc, tDes;
			tSrc.__ll = vData;
			tDes.__l[0] = SwapByte32(tSrc.__l[1]);
			tDes.__l[1] = SwapByte32(tSrc.__l[0]);
			return tDes.__ll;
		}
	#elif defined WIN32
		inline unsigned short SwapByte16(unsigned short vData)
		{
			__asm ror vData, 8
			return vData;
		}
		inline unsigned int SwapByte32(unsigned int vData)
		{
			__asm mov eax, vData
			__asm bswap eax
			__asm mov vData, eax
			return vData;
		}
		inline unsigned long long SwapByte64(unsigned long long vData)
		{
			union
			{
				unsigned long long __ll;
				unsigned int __l[2];
			} tSrc, tDes;
			tSrc.__ll = vData;
			tDes.__l[0] = SwapByte32(tSrc.__l[1]);
			tDes.__l[1] = SwapByte32(tSrc.__l[0]);
			return tDes.__ll;
		}
	#endif
#endif

#endif // !__XSWAPBYTES_H__
