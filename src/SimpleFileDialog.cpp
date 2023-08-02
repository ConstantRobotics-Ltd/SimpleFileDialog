#if defined(linux) || defined(__linux) || defined(__linux__)
#include <cstdint>
#else
#include <Windows.h>
#include <shobjidl.h>
#endif
#include <cstring>
#include <string.h>
#include "SimpleFileDialog.h"



std::string cr::utils::SimpleFileDialog::dialog()
{
#if defined(linux) || defined(__linux) || defined(__linux__)
    char filename[1024];
    FILE *f = popen("zenity --file-selection", "r");
    fgets(filename, 1024, f);
    filename[strlen(filename) - 1] = 0;
    std::string file(filename);
    return file;
#else
    IFileOpenDialog* pFileOpen = nullptr;
    HRESULT hr;
    PWSTR file;
    char* filename;

    hr = CoInitializeEx(NULL,COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (SUCCEEDED(hr))
    {
        hr = CoCreateInstance(CLSID_FileOpenDialog,
                              NULL, CLSCTX_ALL,
                              IID_IFileOpenDialog,
                              (void**)&pFileOpen);
        if (SUCCEEDED(hr))
        {
            pFileOpen->SetTitle(L"OPEN VIDEO FILE");
            hr = pFileOpen->Show(NULL);
            if (SUCCEEDED(hr))
            {
                IShellItem* pItem;
                hr = pFileOpen->GetResult(&pItem);
                if (SUCCEEDED(hr))
                {
                    hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &file);
                    if (SUCCEEDED(hr))
                    {
                        pItem->Release();
                        int count = WideCharToMultiByte(
                        CP_ACP, 0, file, (int)wcslen(file), 0, 0, NULL, NULL);
                        filename = new char[(size_t)count + 1];
                        WideCharToMultiByte(CP_ACP, 0, file, count, filename,
                                            count + 1, NULL, NULL);
                        filename[(size_t)count] = '\0';
                        std::string file(filename);
                        delete[] filename;
                        return file;
                    }
                }
                pItem->Release();
                return "";
            }
        }
    }
    pFileOpen->Release();
    return "";
#endif
}
