#pragma once

#ifndef __XMEMCACHE_H__
#define __XMEMCACHE_H__

#include "XMutex.h"

#ifdef MEM_TRACE
#	define NO_MEM_CACHE
#endif

#ifdef NO_MEM_CACHE
#	define MCALLOC(dw)		malloc(dw)
#	define MCREALLOC(p,dw)	realloc(p,dw)
#	define MCFREE(p)		free(p)
#else
#	define MCALLOC(dw)		g_pMemCache->Alloc(dw)
#	define MCREALLOC(p,dw)	g_pMemCache->ReAlloc(p,dw)
#	define MCFREE(p)		g_pMemCache->Free(p)
#endif

#ifndef SAFE_MCFREE
#	define SAFE_MCFREE(p)	{ if(p) { MCFREE(p); (p) = NULL; } }
#endif

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
template<typename MutexType = XDummyMutex>
class XMemCache
{
public:
	//-----------------------------------------------------------------------------
	void* Alloc(unsigned int dwBytes);

	//-----------------------------------------------------------------------------
	void Free(void* pMem);

	//-----------------------------------------------------------------------------
	void* ReAlloc(void* pMem, unsigned int dwNewBytes);

	//-----------------------------------------------------------------------------
	void* TryAlloc(unsigned int dwBytes);

	//-----------------------------------------------------------------------------
	bool TryFree(void* pMem);

	//-----------------------------------------------------------------------------
	void SetMaxSize(unsigned int dwSize)
	{
		m_dwMaxSize = dwSize;
	}

	//-----------------------------------------------------------------------------
	unsigned int GetFreeSize()
	{
		return m_dwCurrentFreeSize;
	}

	//-----------------------------------------------------------------------------
	unsigned int GetGC()
	{
		return m_dwGCTimes;
	}

	//-----------------------------------------------------------------------------
	void SetTerminate() { m_bTerminate = TRUE; }

	//-----------------------------------------------------------------------------
	void SetMemTraceDesc(void* pMem, const char* szDesc);

	//-----------------------------------------------------------------------------
	XMemCache(unsigned int dwMaxSize = 16 * 1024 * 1024);

	//-----------------------------------------------------------------------------
	~XMemCache();

	//---------------------------------------------------------------------------
	// 尝试垃圾收集
	//---------------------------------------------------------------------------
	void TryGC(unsigned int dwExpectSize);

private:
	//---------------------------------------------------------------------------
	// 垃圾收集
	//---------------------------------------------------------------------------
	void GC(unsigned int dwExpectSize, unsigned int dwUseTime);

	//---------------------------------------------------------------------------
	// 返回最匹配的大小
	//---------------------------------------------------------------------------
	int GetIndex(unsigned int dwSize, unsigned int& dwRealSize);

private:
	// 内存块头描述
	struct tagNode
	{
		tagNode*		pNext;
		tagNode*		pPrev;
		int				nIndex;
		unsigned int	dwSize;
		unsigned int	dwUseTime;
		unsigned int	dwFreeTime;	// 检测用，FreeTime应该等于dwUseTime,防止外部多次Free

#ifdef MEM_DEBUG
		DWORD		dwLastAllocSize;
		CHAR		szMemTraceDesc[LONG_STRING];
#endif

		void*		pMem[1];		// 实际内存空间
	};

	struct
	{
		int			nNodeNum;
		int			nAlloc;

		tagNode*	pFirst;
		tagNode*	pLast;
	} m_Pool[16];


private:
	//---------------------------------------------------------------------------
	MutexType				m_Lock;						// 锁定
	//---------------------------------------------------------------------------
	unsigned int			m_dwMaxSize;				// 外部设定的最大允许空闲内存
	//---------------------------------------------------------------------------
	bool volatile			m_bTerminate;				// 结束标志,结束时为了加速不调用GC
	//---------------------------------------------------------------------------
	unsigned int volatile 	m_dwCurrentFreeSize;		// 内存池中空闲内存总数
	//---------------------------------------------------------------------------
	unsigned int volatile	m_dwGCTimes;				// 统计用，垃圾收集次数
};

//-----------------------------------------------------------------------------
// 全局变量声明
//-----------------------------------------------------------------------------
extern XMemCache<XAtomMutex>*	g_pMemCache;

//-----------------------------------------------------------------------------
// 构造和析构
//-----------------------------------------------------------------------------
template<typename MutexType>
XMemCache<MutexType>::XMemCache(unsigned int dwMaxSize)
	: m_dwMaxSize(dwMaxSize)
	, m_dwCurrentFreeSize(0)
	, m_bTerminate(0)
	, m_dwGCTimes(0)
{
	ZeroMemory(m_Pool, sizeof(m_Pool));
}

template<typename MutexType>
XMemCache<MutexType>::~XMemCache()
{
	for (int n = 0; n < 16; n++)
	{
		while (m_Pool[n].pFirst)
		{
			tagNode* pNode = m_Pool[n].pFirst;
			m_Pool[n].pFirst = m_Pool[n].pFirst->pNext;
			free(pNode);
		}
	}
}

//-----------------------------------------------------------------------------
// 分配
//-----------------------------------------------------------------------------
template<typename MutexType>
void* XMemCache<MutexType>::Alloc(unsigned int dwBytes)
{
	unsigned int dwRealSize = 0;

	int nIndex = GetIndex(dwBytes, dwRealSize);
	if (-1 != nIndex)
	{
		if (m_Pool[nIndex].pFirst)	// 提前尝试
		{
			m_Lock.Lock();
			if (m_Pool[nIndex].pFirst)	// 池里有，就从池里分配
			{
				tagNode* pNode = m_Pool[nIndex].pFirst;
				m_Pool[nIndex].pFirst = m_Pool[nIndex].pFirst->pNext;
				if (m_Pool[nIndex].pFirst != nullptr)
				{
					m_Pool[nIndex].pFirst->pPrev = nullptr;
				}
				else
				{
					m_Pool[nIndex].pLast = nullptr;
				}
				m_dwCurrentFreeSize -= dwRealSize;
				++pNode->dwUseTime;
				--m_Pool[nIndex].nNodeNum;
				++m_Pool[nIndex].nAlloc;
				m_Lock.Unlock();

#ifdef MEM_DEBUG
				for (DWORD n = 0; n<pNode->dwSize; ++n)
				{
					if (((BYTE*)pNode->pMem)[n] != 0xCD)
					{
						ASSERT(0);
					}
				}

				pNode->dwLastAllocSize = dwBytes;
#endif
				return pNode->pMem;
			}
			m_Lock.Unlock();
		}

		tagNode* pNode = (tagNode*)malloc(dwRealSize + sizeof(tagNode));
		if (!pNode)
		{
			return nullptr;
		}

		pNode->nIndex = nIndex;
		pNode->dwSize = dwRealSize;
		pNode->dwUseTime = 0;
		pNode->dwFreeTime = 0;	// // 检测用，FreeTime应该等于dwUseTime,防止外部多次Free
		*(unsigned int*)((unsigned char*)pNode->pMem + dwRealSize) = 0xDeadBeef;
		return pNode->pMem;	// 从实际内存中分配
	}

	return nullptr;
}


//-----------------------------------------------------------------------------
// 释放
//-----------------------------------------------------------------------------
template<typename MutexType>
void XMemCache<MutexType>::Free(void* pMem)
{
	if (pMem == nullptr)
	{
		return;
	}

	tagNode* pNode = (tagNode*)(((unsigned char*)pMem) - sizeof(tagNode) + sizeof(void*));

	if (m_bTerminate)	// 结束时，直接归还
	{
		free(pNode);
		return;
	}

	if (-1 != pNode->nIndex)
	{
		if (pNode->dwSize + m_dwCurrentFreeSize > m_dwMaxSize)
		{
			GC(pNode->dwSize * 2, pNode->dwUseTime);	// 垃圾收集
		}

		if (pNode->dwSize + m_dwCurrentFreeSize <= m_dwMaxSize) // 内存池可以容纳
		{
			if (*(unsigned int*)((unsigned char*)pNode->pMem + pNode->dwSize) != 0xDeadBeef)
			{
				printf("MemCache node corruption!");
				DebugBreak();
			}

			m_Lock.Lock();

			// ------------------------------------
			pNode->pPrev = nullptr;
			pNode->pNext = m_Pool[pNode->nIndex].pFirst;
			if (pNode->pNext == pNode)
			{
				printf("MemCache Free more than once!");
				DebugBreak(); // 重复释放
			}

			if (m_Pool[pNode->nIndex].pFirst)
			{
				m_Pool[pNode->nIndex].pFirst->pPrev = pNode;
			}
			else
			{
				m_Pool[pNode->nIndex].pLast = pNode;
			}

			m_Pool[pNode->nIndex].pFirst = pNode;
			++m_Pool[pNode->nIndex].nNodeNum;
			m_dwCurrentFreeSize += pNode->dwSize;

			if (pNode->dwFreeTime != pNode->dwUseTime)
			{
				printf("MemCache Free more than once!");
				DebugBreak();
			}
			++pNode->dwFreeTime;	// 检测用，FreeTime应该等于dwUseTime,防止外部多次Free

#ifdef MEM_DEBUG
			memset(pNode->pMem, 0xCD, pNode->dwSize);
#endif

			// ------------------------------------
			m_Lock.Unlock();
			return;
		}
	}

	free(pNode);
}


//-----------------------------------------------------------------------------
// 再分配
//-----------------------------------------------------------------------------
template<typename MutexType>
void* XMemCache<MutexType>::ReAlloc(void* pMem, unsigned int dwNewBytes)
{
	if (pMem == nullptr)
	{
		return nullptr;
	}

	// 分配新内存
	void* pNew = Alloc(dwNewBytes);
	if (pNew == nullptr)
	{
		return nullptr;
	}

	// 取得原大小并拷贝
	tagNode* pNode = (tagNode*)(((unsigned char*)pMem) - sizeof(tagNode) + sizeof(void*));
	memcpy(pNew, pMem, fxmin(pNode->dwSize, dwNewBytes));

	// 释放原内存
	Free(pMem);
	return pNew;
}


//-----------------------------------------------------------------------------
// 分配
//-----------------------------------------------------------------------------
template<typename MutexType>
void* XMemCache<MutexType>::TryAlloc(unsigned int dwBytes)
{
	unsigned int dwRealSize = 0;
	int nIndex = GetIndex(dwBytes, dwRealSize);
	if (-1 != nIndex)
	{
		if (!m_Lock.TryLock())
		{
			return nullptr;
		}

		if (m_Pool[nIndex].pFirst)	// 池里有，就从池里分配
		{
			tagNode* pNode = m_Pool[nIndex].pFirst;
			m_Pool[nIndex].pFirst = m_Pool[nIndex].pFirst->pNext;
			if (m_Pool[nIndex].pFirst)
			{
				m_Pool[nIndex].pFirst->pPrev = nullptr;
			}
			else
			{
				m_Pool[nIndex].pLast = nullptr;
			}
			m_dwCurrentFreeSize -= dwRealSize;
			++pNode->dwUseTime;
			--m_Pool[nIndex].nNodeNum;
			++m_Pool[nIndex].nAlloc;
			m_Lock.Unlock();
			return pNode->pMem;
		}
		m_Lock.Unlock();

		tagNode* pNode = (tagNode*)malloc(dwRealSize + sizeof(tagNode));
		if (!pNode)
		{
			return nullptr;
		}
		pNode->nIndex = nIndex;
		pNode->dwSize = dwRealSize;
		pNode->dwUseTime = 0;
		pNode->dwFreeTime = 0;	// // 检测用，FreeTime应该等于dwUseTime,防止外部多次Free
		*(unsigned int*)((unsigned char*)pNode->pMem + dwRealSize) = 0xDeadBeef;
		return pNode->pMem;	// 从实际内存中分配
	}

	return nullptr
}


//-----------------------------------------------------------------------------
// 释放
//-----------------------------------------------------------------------------
template<typename MutexType>
bool XMemCache<MutexType>::TryFree(void* pMem)
{
	if (pMem == nullptr)
	{
		return true;
	}

	tagNode* pNode = (tagNode*)(((unsigned char*)pMem) - sizeof(tagNode) + sizeof(void*));

	if (m_bTerminate)	// 结束时，直接归还
	{
		free(pNode);
		return true;
	}

	if (-1 != pNode->nIndex)
	{
		if (pNode->dwSize + m_dwCurrentFreeSize > m_dwMaxSize)
		{
			GC(pNode->dwSize * 2, pNode->dwUseTime);	// 垃圾收集
		}

		if (pNode->dwSize + m_dwCurrentFreeSize <= m_dwMaxSize) // 内存池可以容纳
		{
			if (*(unsigned int*)((unsigned char*)pNode->pMem + pNode->dwSize) != 0xDeadBeef)
			{
				printf("MemCache node corruption!");
				DebugBreak();
			}

			if (!m_Lock.TryLock())
			{
				return false;
			}
			pNode->pPrev = nullptr;
			pNode->pNext = m_Pool[pNode->nIndex].pFirst;

			if (pNode->pNext == pNode)
			{
				printf("MemCache Free more than once!");
				DebugBreak(); // 重复释放
			}

			if (m_Pool[pNode->nIndex].pFirst)
			{
				m_Pool[pNode->nIndex].pFirst->pPrev = pNode;
			}
			else
			{
				m_Pool[pNode->nIndex].pLast = pNode;
			}

			m_Pool[pNode->nIndex].pFirst = pNode;
			m_dwCurrentFreeSize += pNode->dwSize;
			++m_Pool[pNode->nIndex].nNodeNum;

			if (pNode->dwFreeTime != pNode->dwUseTime)
			{
				printf("MemCache Free more than once!");
				DebugBreak();
			}
			++pNode->dwFreeTime;	// 检测用，FreeTime应该等于dwUseTime,防止外部多次Free

			m_Lock.Unlock();
			return true;
		}
	}

	free(pNode);
	return true;
}


//-----------------------------------------------------------------------------
// 垃圾收集
//-----------------------------------------------------------------------------
template<typename MutexType>
void XMemCache<MutexType>::GC(unsigned int dwExpectSize, unsigned int dwUseTime)
{
	unsigned int dwFreeTime = 0;

	if (dwExpectSize > m_dwMaxSize / 64)
	{
		dwExpectSize = m_dwMaxSize / 64;	// 一次不要释放太多
	}

	unsigned int dwFreeSize = 0;

	m_Lock.Lock();
	++m_dwGCTimes;
	for (int n = 15; n >= 0; --n)	// 从最大的开始回收
	{
		if (!m_Pool[n].pFirst)
		{
			continue;
		}

		tagNode* pNode = m_Pool[n].pLast; // 从最后开始释放，因为后面的Node使用次数少
		while (pNode)
		{
			tagNode* pTempNode = pNode;
			pNode = pNode->pPrev;

			if (pTempNode->dwUseTime >= dwUseTime)
			{
				break;	// 再往前已经没有适合释放的节点了，跳出看看其他型号
			}

						// 释放此节点
			if (pNode)
			{
				pNode->pNext = pTempNode->pNext;
			}

			if (pTempNode->pNext)
			{
				pTempNode->pNext->pPrev = pNode;
			}

			if (m_Pool[n].pLast == pTempNode)
			{
				m_Pool[n].pLast = pNode;
			}

			if (m_Pool[n].pFirst == pTempNode)
			{
				m_Pool[n].pFirst = pTempNode->pNext;
			}

			m_dwCurrentFreeSize -= pTempNode->dwSize;
			--m_Pool[n].nNodeNum;
			dwFreeSize += pTempNode->dwSize;

			++dwFreeTime;
			free(pTempNode);

			if (dwFreeSize >= dwExpectSize || dwFreeTime > 32)	// 每次GC不要调用太多Free
			{
				m_Lock.Unlock();
				return;
			}
		}
	}

	m_Lock.Unlock();
}


//-----------------------------------------------------------------------------
// 匹配大小
//-----------------------------------------------------------------------------
template<typename MutexType>
int XMemCache<MutexType>::GetIndex(unsigned int dwSize, unsigned int& dwRealSize)
{
	if (dwSize <= 32) { dwRealSize = 32;			return 0; }
	if (dwSize <= 64) { dwRealSize = 64;			return 1; }
	if (dwSize <= 128) { dwRealSize = 128;			return 2; }
	if (dwSize <= 256) { dwRealSize = 256;			return 3; }
	if (dwSize <= 512) { dwRealSize = 512;			return 4; }
	if (dwSize <= 1024) { dwRealSize = 1024;		return 5; }		//1k
	if (dwSize <= 2048) { dwRealSize = 2048;		return 6; }		//2k
	if (dwSize <= 4096) { dwRealSize = 4096;		return 7; }		//4k
	if (dwSize <= 8192) { dwRealSize = 8192;		return 8; }		//8k
	if (dwSize <= 16384) { dwRealSize = 16384;		return 9; }		//16k
	if (dwSize <= 32768) { dwRealSize = 32768;		return 10; }	//32k
	if (dwSize <= 65536) { dwRealSize = 65536;		return 11; }	//64k
	if (dwSize <= 131072) { dwRealSize = 131072;	return 12; }	//128k
	if (dwSize <= 262144) { dwRealSize = 262144;	return 13; }	//256k
	if (dwSize <= 524288) { dwRealSize = 524288;	return 14; }	//512k
	if (dwSize <= 1048576) { dwRealSize = 1048576;	return 15; }	//1M
	dwRealSize = dwSize;
	return -1;
}


//-----------------------------------------------------------------------------
// 调试设置
//-----------------------------------------------------------------------------
template<typename MutexType>
void XMemCache<MutexType>::SetMemTraceDesc(void* pMem, const char* szDesc)
{
#ifdef MEM_DEBUG
	tagNode* pNode = (tagNode*)(((LPBYTE)pMem) - sizeof(tagNode) + sizeof(LPVOID));
	strncpy(pNode->szMemTraceDesc, szDesc, LONG_STRING);
#endif
}

//-----------------------------------------------------------------------------
// 垃圾收集
//-----------------------------------------------------------------------------
template<typename MutexType>
void XMemCache<MutexType>::TryGC(unsigned int dwExpectSize)
{
	static const unsigned int MAX_FREE = 32;
	tagNode* free_array[MAX_FREE];
	unsigned int dwFreeTime = 0;

	if (dwExpectSize > m_dwMaxSize / 64)
	{
		dwExpectSize = m_dwMaxSize / 64;	// 一次不要释放太多
	}

	unsigned int dwFreeSize = 0;

	if (!m_Lock.TryLock())
	{
		return;
	}

	++m_dwGCTimes;
	for (int n = 15; n >= 0; --n)	// 从最大的开始回收
	{
		if (!m_Pool[n].pFirst)
		{
			continue;
		}

		tagNode* pNode = m_Pool[n].pLast; // 从最后开始释放，因为后面的Node使用次数少
		while (pNode)
		{
			tagNode* pTempNode = pNode;
			pNode = pNode->pPrev;

			// 释放此节点
			if (pNode)
			{
				pNode->pNext = pTempNode->pNext;
			}

			if (pTempNode->pNext)
			{
				pTempNode->pNext->pPrev = pNode;
			}

			if (m_Pool[n].pLast == pTempNode)
			{
				m_Pool[n].pLast = pNode;
			}

			if (m_Pool[n].pFirst == pTempNode)
			{
				m_Pool[n].pFirst = pTempNode->pNext;
			}

			m_dwCurrentFreeSize -= pTempNode->dwSize;
			--m_Pool[n].nNodeNum;
			dwFreeSize += pTempNode->dwSize;

			// 记录到数组，等会退出临界区再释放
			free_array[dwFreeTime++] = pTempNode;

			if (dwFreeSize >= dwExpectSize || dwFreeTime >= MAX_FREE)	// 每次GC不要调用太多Free
			{
				goto __out_gc;
			}
		}
	}

__out_gc:
	m_Lock.Unlock();

	for (unsigned int n = 0; n < dwFreeTime; ++n)
	{
		free(free_array[n]);
	}
}


//---------------------------------------------------------------------------
//从内存池分配的对象基类
//---------------------------------------------------------------------------
class XMemCacheObj
{
public:
#ifndef MEM_TRACE
	void*			operator new(unsigned int size) { return MCALLOC((unsigned int)size); }
	void*			operator new[](unsigned int size) { return MCALLOC((unsigned int)size); }
	void			operator delete(void* p) { MCFREE(p); }
	void			operator delete[](void* p) { MCFREE(p); }
#endif
};

#endif // !__XMEMCACHE_H__
