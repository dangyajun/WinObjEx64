/*******************************************************************************
*
*  (C) COPYRIGHT AUTHORS, 2020 - 2021
*
*  TITLE:       SUP.C
*
*  VERSION:     1.01
*
*  DATE:        08 Jan 2021
*
* THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
* ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED
* TO THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
* PARTICULAR PURPOSE.
*
*******************************************************************************/

#include "global.h"

PVOID HeapMemoryAlloc(_In_ SIZE_T Size)
{
    return HeapAlloc(g_ctx.PluginHeap, HEAP_ZERO_MEMORY, Size);
}

BOOL HeapMemoryFree(_In_ PVOID Memory)
{
    if (Memory == NULL) return FALSE;
    return HeapFree(g_ctx.PluginHeap, 0, Memory);
}

#define DBUFFER_SIZE 512

/*
* supConvertFileName
*
* Purpose:
*
* Translate Nt path name to Dos path name.
*
*/
BOOL supConvertFileName(
    _In_ LPWSTR NtFileName,
    _In_ LPWSTR DosFileName,
    _In_ SIZE_T ccDosFileName
)
{
    BOOL    bSuccess = FALSE, bFound = FALSE;
    WCHAR   szDrive[3];
    WCHAR   szName[MAX_PATH + 1]; //for the device partition name
    WCHAR   szTemp[DBUFFER_SIZE]; //for the disk array
    UINT    uNameLen = 0;
    WCHAR* p;
    SIZE_T  l = 0, k = 0;

    if ((NtFileName == NULL) || (DosFileName == NULL) || (ccDosFileName < MAX_PATH))
        return bSuccess;

    szDrive[0] = L'X';
    szDrive[1] = L':';
    szDrive[2] = 0;

    RtlSecureZeroMemory(szTemp, sizeof(szTemp));

    uNameLen = GetLogicalDriveStrings(DBUFFER_SIZE - 1, szTemp);
    if (uNameLen == 0)
        return bSuccess;

    p = szTemp;

    do {

        *szDrive = *p;

        RtlSecureZeroMemory(szName, sizeof(szName));

        if (QueryDosDevice(szDrive, szName, MAX_PATH)) {

            uNameLen = (UINT)_strlen(szName);

            if (uNameLen < MAX_PATH) {

                bFound = (_strncmp(NtFileName, szName, uNameLen) == 0);

                if (bFound && *(NtFileName + uNameLen) == TEXT('\\')) {

                    _strcpy(DosFileName, szDrive);
                    l = _strlen(DosFileName);
                    k = _strlen(NtFileName);

                    _strncpy(&DosFileName[l],
                        ccDosFileName - l,
                        NtFileName + uNameLen,
                        k - uNameLen);

                    bSuccess = TRUE;
                    break;
                }
            }

        }

        while (*p++);

    } while (!bFound && *p); // end of string
    return bSuccess;
}

/*
* supGetWin32FileName
*
* Purpose:
*
* Query filename by handle.
*
* Input buffer must be at least MAX_PATH length.
*
*/
BOOL supGetWin32FileName(
    _In_ LPWSTR FileName,
    _Inout_ LPWSTR Win32FileName,
    _In_ SIZE_T ccWin32FileName
)
{
    BOOL                bResult = FALSE;
    NTSTATUS            status = STATUS_UNSUCCESSFUL;
    HANDLE              hFile = NULL;
    UNICODE_STRING      NtFileName;
    OBJECT_ATTRIBUTES   obja;
    IO_STATUS_BLOCK     iost;
    ULONG               memIO;
    BYTE* Buffer = NULL;

    if ((Win32FileName == NULL) || (ccWin32FileName < MAX_PATH)) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    do {

        RtlInitUnicodeString(&NtFileName, FileName);
        InitializeObjectAttributes(&obja, &NtFileName, OBJ_CASE_INSENSITIVE, 0, NULL);

        status = NtCreateFile(&hFile, SYNCHRONIZE, &obja, &iost, NULL, 0,
            FILE_SHARE_VALID_FLAGS, FILE_OPEN,
            FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0);

        if (!NT_SUCCESS(status))
            break;

        memIO = 0;
        status = NtQueryObject(hFile, ObjectNameInformation, NULL, 0, &memIO);
        if (status != STATUS_INFO_LENGTH_MISMATCH)
            break;

        Buffer = (BYTE*)HeapMemoryAlloc(memIO);
        if (Buffer == NULL)
            break;

        status = NtQueryObject(hFile, ObjectNameInformation, Buffer, memIO, NULL);
        if (!NT_SUCCESS(status))
            break;

        if (!supConvertFileName(((PUNICODE_STRING)Buffer)->Buffer, Win32FileName, ccWin32FileName))
            break;

        bResult = TRUE;

    } while (FALSE);

    if (hFile)
        NtClose(hFile);

    if (Buffer != NULL)
        HeapMemoryFree(Buffer);

    return bResult;
}

/*
* supClipboardCopy
*
* Purpose:
*
* Copy text to the clipboard.
*
*/
VOID supClipboardCopy(
    _In_ LPWSTR lpText,
    _In_ SIZE_T cbText
)
{
    LPWSTR  lptstrCopy;
    HGLOBAL hglbCopy;
    SIZE_T  dwSize;

    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        dwSize = cbText + sizeof(UNICODE_NULL);
        hglbCopy = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, dwSize);
        if (hglbCopy != NULL) {
            lptstrCopy = (LPWSTR)GlobalLock(hglbCopy);
            if (lptstrCopy) {
                RtlCopyMemory(lptstrCopy, lpText, cbText);
            }
            GlobalUnlock(hglbCopy);
            if (!SetClipboardData(CF_UNICODETEXT, hglbCopy))
                GlobalFree(hglbCopy);
        }
        CloseClipboard();
    }
}

/*
* supCopyTreeListSubItemValue
*
* Purpose:
*
* Copy treelist value to the clipboard.
*
*/
VOID supCopyTreeListSubItemValue(
    _In_ HWND TreeList,
    _In_ UINT ValueIndex
)
{
    SIZE_T             cbText;
    LPWSTR             lpText;
    TL_SUBITEMS_FIXED* subitems = NULL;
    TVITEMEX           itemex;
    WCHAR              textbuf[MAX_PATH + 1];

    __try {

        RtlSecureZeroMemory(&itemex, sizeof(itemex));
        RtlSecureZeroMemory(textbuf, sizeof(textbuf));
        itemex.mask = TVIF_TEXT;
        itemex.hItem = TreeList_GetSelection(TreeList);
        itemex.pszText = textbuf;
        itemex.cchTextMax = MAX_PATH;

        TreeList_GetTreeItem(TreeList, &itemex, &subitems);

        if (subitems) {
            if (ValueIndex < subitems->Count) {
                lpText = subitems->Text[ValueIndex];
                if (lpText) {
                    cbText = _strlen(lpText) * sizeof(WCHAR);
                    supClipboardCopy(lpText, cbText);
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
}

/*
* supGetItemText
*
* Purpose:
*
* Returns buffer with text from the given listview item.
*
* Returned buffer must be freed with HeapMemoryFree after usage.
*
*/
LPWSTR supGetItemText(
    _In_ HWND ListView,
    _In_ INT nItem,
    _In_ INT nSubItem,
    _Out_opt_ PSIZE_T lpSize //length in bytes
)
{
    INT     len;
    LPARAM  sz = 0;
    LV_ITEM item;

    RtlSecureZeroMemory(&item, sizeof(item));

    item.iItem = nItem;
    item.iSubItem = nSubItem;
    len = 128;
    do {
        len *= 2;
        item.cchTextMax = len;
        if (item.pszText) {
            HeapMemoryFree(item.pszText);
            item.pszText = NULL;
        }
        item.pszText = (LPWSTR)HeapMemoryAlloc(len * sizeof(WCHAR));
        sz = SendMessage(ListView, LVM_GETITEMTEXT, (WPARAM)item.iItem, (LPARAM)&item);
    } while (sz == (LPARAM)len - 1);

    //empty string
    if (sz == 0) {
        if (item.pszText) {
            HeapMemoryFree(item.pszText);
            item.pszText = NULL;
        }
    }

    if (lpSize) {
        *lpSize = sz * sizeof(WCHAR);
    }
    return item.pszText;
}

/*
* supGetItemText2
*
* Purpose:
*
* Returns text from the given listview item.
*
*/
LPWSTR supGetItemText2(
    _In_ HWND ListView,
    _In_ INT nItem,
    _In_ INT nSubItem,
    _In_ WCHAR* pszText,
    _In_ UINT cchText
)
{
    LV_ITEM item;

    RtlSecureZeroMemory(&item, sizeof(item));

    item.iItem = nItem;
    item.iSubItem = nSubItem;
    item.pszText = pszText;
    item.cchTextMax = (SIZE_T)cchText;
    SendMessage(ListView, LVM_GETITEMTEXT, (WPARAM)item.iItem, (LPARAM)&item);

    return item.pszText;
}

/*
* supListViewAddCopyValueItem
*
* Purpose:
*
* Add copy to clipboard menu item depending on hit column.
*
*/
BOOL supListViewAddCopyValueItem(
    _In_ HMENU hMenu,
    _In_ HWND hwndLv,
    _In_ UINT uId,
    _In_opt_ UINT uPos,
    _In_ POINT* lpPoint,
    _Out_ INT* pItemHit,
    _Out_ INT* pColumnHit
)
{
    LVHITTESTINFO lvht;
    LVCOLUMN lvc;
    WCHAR szItem[MAX_PATH * 2];
    WCHAR szColumn[MAX_PATH + 1];

    *pColumnHit = -1;
    *pItemHit = -1;

    RtlSecureZeroMemory(&lvht, sizeof(lvht));
    lvht.pt.x = lpPoint->x;
    lvht.pt.y = lpPoint->y;
    ScreenToClient(hwndLv, &lvht.pt);
    if (ListView_SubItemHitTest(hwndLv, &lvht) == -1)
        return FALSE;

    RtlSecureZeroMemory(&lvc, sizeof(lvc));
    RtlSecureZeroMemory(&szColumn, sizeof(szColumn));

    lvc.mask = LVCF_TEXT;
    lvc.pszText = szColumn;
    lvc.cchTextMax = MAX_PATH;
    if (ListView_GetColumn(hwndLv, lvht.iSubItem, &lvc)) {
        _strcpy(szItem, TEXT("Copy \""));
        _strcat(szItem, szColumn);
        _strcat(szItem, TEXT("\""));
        if (InsertMenu(hMenu, uPos, MF_BYCOMMAND, uId, szItem)) {
            *pColumnHit = lvht.iSubItem;
            *pItemHit = lvht.iItem;
            return TRUE;
        }
    }

    return FALSE;
}

/*
* supListViewCopyItemValueToClipboard
*
* Purpose:
*
* Copy selected item text to the clipboard.
*
*/
BOOL supListViewCopyItemValueToClipboard(
    _In_ HWND hwndListView,
    _In_ INT iItem,
    _In_ INT iSubItem
)
{
    SIZE_T cbText;
    LPWSTR lpText;

    if ((iSubItem < 0) || (iItem < 0))
        return FALSE;

    lpText = supGetItemText(hwndListView,
        iItem,
        iSubItem,
        NULL);

    if (lpText) {
        cbText = _strlen(lpText) * sizeof(WCHAR);
        supClipboardCopy(lpText, cbText);
        HeapMemoryFree(lpText);
        return TRUE;
    }
    else {
        if (OpenClipboard(NULL)) {
            EmptyClipboard();
            CloseClipboard();
        }
    }

    return FALSE;
}

/*
* supGetMaxCompareTwoFixedStrings
*
* Purpose:
*
* Returned value used in listview comparer functions.
*
*/
INT supGetMaxCompareTwoFixedStrings(
    _In_ HWND ListView,
    _In_ LPARAM lParam1,
    _In_ LPARAM lParam2,
    _In_ LPARAM lParamSort,
    _In_ BOOL Inverse
)
{
    INT       nResult;
    LPWSTR    lpItem1 = NULL, lpItem2 = NULL, FirstToCompare, SecondToCompare;
    WCHAR     szString1[MAX_PATH + 1], szString2[MAX_PATH + 1];

    szString1[0] = 0;

    lpItem1 = supGetItemText2(
        ListView,
        (INT)lParam1,
        (INT)lParamSort,
        szString1,
        MAX_PATH);

    szString2[0] = 0;

    lpItem2 = supGetItemText2(
        ListView,
        (INT)lParam2,
        (INT)lParamSort,
        szString2,
        MAX_PATH);

    if (Inverse) {
        FirstToCompare = lpItem2;
        SecondToCompare = lpItem1;
    }
    else {
        FirstToCompare = lpItem1;
        SecondToCompare = lpItem2;
    }

    nResult = _strcmpi(FirstToCompare, SecondToCompare);

    return nResult;
}

/*
* supGetMaxOfTwoU64FromHex
*
* Purpose:
*
* Returned value used in listview comparer functions.
*
*/
INT supGetMaxOfTwoU64FromHex(
    _In_ HWND ListView,
    _In_ LPARAM lParam1,
    _In_ LPARAM lParam2,
    _In_ LPARAM lParamSort,
    _In_ BOOL Inverse
)
{
    INT       nResult;
    LPWSTR    lpItem1 = NULL, lpItem2 = NULL;
    ULONG_PTR ad1, ad2;
    WCHAR     szText[32];

    RtlSecureZeroMemory(&szText, sizeof(szText));

    lpItem1 = supGetItemText2(
        ListView,
        (INT)lParam1,
        (INT)lParamSort,
        szText,
        RTL_NUMBER_OF(szText));

    ad1 = hextou64(&lpItem1[2]);

    RtlSecureZeroMemory(&szText, sizeof(szText));

    lpItem2 = supGetItemText2(
        ListView,
        (INT)lParam2,
        (INT)lParamSort,
        szText,
        RTL_NUMBER_OF(szText));

    ad2 = hextou64(&lpItem2[2]);

    if (Inverse)
        nResult = ad1 < ad2;
    else
        nResult = ad1 > ad2;

    return nResult;
}
