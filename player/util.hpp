
#ifndef UTIL_HPP
#define UTIL_HPP

template <typename T>
void SafeRelease(T* &pT)
{
	if (pT)
	{
		pT->Release();
		pT = NULL;
	}
}

#endif /* UTIL_HPP */
