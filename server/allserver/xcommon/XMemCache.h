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
	// ���������ռ�
	//---------------------------------------------------------------------------
	void TryGC(unsigned int dwExpectSize);

private:
	//---------------------------------------------------------------------------
	// �����ռ�
	//---------------------------------------------------------------------------
	void GC(unsigned int dwExpectSize, unsigned int dwUseTime);

	//---------------------------------------------------------------------------
	// ������ƥ��Ĵ�С
	//---------------------------------------------------------------------------
	int GetIndex(unsigned int dwSize, unsigned int& dwRealSize);

private:
	// �ڴ��ͷ����
	struct tagNode
	{
		tagNode*		pNext;
		tagNode*		pPrev;
		int				nIndex;
		unsigned int	dwSize;
		unsigned int	dwUseTime;
		unsigned int	dwFreeTime;	// ����ã�FreeTimeӦ�õ���dwUseTime,��ֹ�ⲿ���Free

#ifdef MEM_DEBUG
		DWORD		dwLastAllocSize;
		CHAR		szMemTraceDesc[LONG_STRING];
#endif

		void*		pMem[1];		// ʵ���ڴ�ռ�
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
	MutexType				m_Lock;						// ����
	//---------------------------------------------------------------------------
	unsigned int			m_dwMaxSize;				// �ⲿ�趨�������������ڴ�
	//---------------------------------------------------------------------------
	bool volatile			m_bTerminate;				// ������־,����ʱΪ�˼��ٲ�����GC
	//---------------------------------------------------------------------------
	unsigned int volatile 	m_dwCurrentFreeSize;		// �ڴ���п����ڴ�����
	//---------------------------------------------------------------------------
	unsigned int volatile	m_dwGCTimes;				// ͳ���ã������ռ�����
};

//-----------------------------------------------------------------------------
// ȫ�ֱ�������
//-----------------------------------------------------------------------------
extern XMemCache<XAtomMutex>*	g_pMemCache;

//-----------------------------------------------------------------------------
// ���������
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
// ����
//-----------------------------------------------------------------------------
template<typename MutexType>
void* XMemCache<MutexType>::Alloc(unsigned int dwBytes)
{
	unsigned int dwRealSize = 0;

	int nIndex = GetIndex(dwBytes, dwRealSize);
	if (-1 != nIndex)
	{
		if (m_Pool[nIndex].pFirst)	// ��ǰ����
		{
			m_Lock.Lock();
			if (m_Pool[nIndex].pFirst)	// �����У��ʹӳ������
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
		pNode->dwFreeTime = 0;	// // ����ã�FreeTimeӦ�õ���dwUseTime,��ֹ�ⲿ���Free
		*(unsigned int*)((unsigned char*)pNode->pMem + dwRealSize) = 0xDeadBeef;
		return pNode->pMem;	// ��ʵ���ڴ��з���
	}

	return nullptr;
}


//-----------------------------------------------------------------------------
// �ͷ�
//-----------------------------------------------------------------------------
template<typename MutexType>
void XMemCache<MutexType>::Free(void* pMem)
{
	if (pMem == nullptr)
	{
		return;
	}

	tagNode* pNode = (tagNode*)(((unsigned char*)pMem) - sizeof(tagNode) + sizeof(void*));

	if (m_bTerminate)	// ����ʱ��ֱ�ӹ黹
	{
		free(pNode);
		return;
	}

	if (-1 != pNode->nIndex)
	{
		if (pNode->dwSize + m_dwCurrentFreeSize > m_dwMaxSize)
		{
			GC(pNode->dwSize * 2, pNode->dwUseTime);	// �����ռ�
		}

		if (pNode->dwSize + m_dwCurrentFreeSize <= m_dwMaxSize) // �ڴ�ؿ�������
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
				DebugBreak(); // �ظ��ͷ�
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
			++pNode->dwFreeTime;	// ����ã�FreeTimeӦ�õ���dwUseTime,��ֹ�ⲿ���Free

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
// �ٷ���
//-----------------------------------------------------------------------------
template<typename MutexType>
void* XMemCache<MutexType>::ReAlloc(void* pMem, unsigned int dwNewBytes)
{
	if (pMem == nullptr)
	{
		return nullptr;
	}

	// �������ڴ�
	void* pNew = Alloc(dwNewBytes);
	if (pNew == nullptr)
	{
		return nullptr;
	}

	// ȡ��ԭ��С������
	tagNode* pNode = (tagNode*)(((unsigned char*)pMem) - sizeof(tagNode) + sizeof(void*));
	memcpy(pNew, pMem, fxmin(pNode->dwSize, dwNewBytes));

	// �ͷ�ԭ�ڴ�
	Free(pMem);
	return pNew;
}


//-----------------------------------------------------------------------------
// ����
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

		if (m_Pool[nIndex].pFirst)	// �����У��ʹӳ������
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
		pNode->dwFreeTime = 0;	// // ����ã�FreeTimeӦ�õ���dwUseTime,��ֹ�ⲿ���Free
		*(unsigned int*)((unsigned char*)pNode->pMem + dwRealSize) = 0xDeadBeef;
		return pNode->pMem;	// ��ʵ���ڴ��з���
	}

	return nullptr
}


//-----------------------------------------------------------------------------
// �ͷ�
//-----------------------------------------------------------------------------
template<typename MutexType>
bool XMemCache<MutexType>::TryFree(void* pMem)
{
	if (pMem == nullptr)
	{
		return true;
	}

	tagNode* pNode = (tagNode*)(((unsigned char*)pMem) - sizeof(tagNode) + sizeof(void*));

	if (m_bTerminate)	// ����ʱ��ֱ�ӹ黹
	{
		free(pNode);
		return true;
	}

	if (-1 != pNode->nIndex)
	{
		if (pNode->dwSize + m_dwCurrentFreeSize > m_dwMaxSize)
		{
			GC(pNode->dwSize * 2, pNode->dwUseTime);	// �����ռ�
		}

		if (pNode->dwSize + m_dwCurrentFreeSize <= m_dwMaxSize) // �ڴ�ؿ�������
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
				DebugBreak(); // �ظ��ͷ�
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
			++pNode->dwFreeTime;	// ����ã�FreeTimeӦ�õ���dwUseTime,��ֹ�ⲿ���Free

			m_Lock.Unlock();
			return true;
		}
	}

	free(pNode);
	return true;
}


//-----------------------------------------------------------------------------
// �����ռ�
//-----------------------------------------------------------------------------
template<typename MutexType>
void XMemCache<MutexType>::GC(unsigned int dwExpectSize, unsigned int dwUseTime)
{
	unsigned int dwFreeTime = 0;

	if (dwExpectSize > m_dwMaxSize / 64)
	{
		dwExpectSize = m_dwMaxSize / 64;	// һ�β�Ҫ�ͷ�̫��
	}

	unsigned int dwFreeSize = 0;

	m_Lock.Lock();
	++m_dwGCTimes;
	for (int n = 15; n >= 0; --n)	// �����Ŀ�ʼ����
	{
		if (!m_Pool[n].pFirst)
		{
			continue;
		}

		tagNode* pNode = m_Pool[n].pLast; // �����ʼ�ͷţ���Ϊ�����Nodeʹ�ô�����
		while (pNode)
		{
			tagNode* pTempNode = pNode;
			pNode = pNode->pPrev;

			if (pTempNode->dwUseTime >= dwUseTime)
			{
				break;	// ����ǰ�Ѿ�û���ʺ��ͷŵĽڵ��ˣ��������������ͺ�
			}

						// �ͷŴ˽ڵ�
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

			if (dwFreeSize >= dwExpectSize || dwFreeTime > 32)	// ÿ��GC��Ҫ����̫��Free
			{
				m_Lock.Unlock();
				return;
			}
		}
	}

	m_Lock.Unlock();
}


//-----------------------------------------------------------------------------
// ƥ���С
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
// ��������
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
// �����ռ�
//-----------------------------------------------------------------------------
template<typename MutexType>
void XMemCache<MutexType>::TryGC(unsigned int dwExpectSize)
{
	static const unsigned int MAX_FREE = 32;
	tagNode* free_array[MAX_FREE];
	unsigned int dwFreeTime = 0;

	if (dwExpectSize > m_dwMaxSize / 64)
	{
		dwExpectSize = m_dwMaxSize / 64;	// һ�β�Ҫ�ͷ�̫��
	}

	unsigned int dwFreeSize = 0;

	if (!m_Lock.TryLock())
	{
		return;
	}

	++m_dwGCTimes;
	for (int n = 15; n >= 0; --n)	// �����Ŀ�ʼ����
	{
		if (!m_Pool[n].pFirst)
		{
			continue;
		}

		tagNode* pNode = m_Pool[n].pLast; // �����ʼ�ͷţ���Ϊ�����Nodeʹ�ô�����
		while (pNode)
		{
			tagNode* pTempNode = pNode;
			pNode = pNode->pPrev;

			// �ͷŴ˽ڵ�
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

			// ��¼�����飬�Ȼ��˳��ٽ������ͷ�
			free_array[dwFreeTime++] = pTempNode;

			if (dwFreeSize >= dwExpectSize || dwFreeTime >= MAX_FREE)	// ÿ��GC��Ҫ����̫��Free
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
//���ڴ�ط���Ķ������
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
