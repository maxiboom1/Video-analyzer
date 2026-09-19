#include "StartupAuth.h"

#include "Resources.h"
#include "Version.h"

#include <string>

namespace
{
    constexpr std::wstring_view kOwnerPassword = L"Kisa1720!";
    constexpr std::wstring_view kClientPassword = L"SegevSportCompany";
    constexpr int kPasswordLimit = 128;

    INT_PTR CALLBACK PasswordDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_INITDIALOG:
        {
            const std::string title = std::string(kAppWindowTitleA) + " - Sign in";
            SetWindowTextA(dialog, title.c_str());
            SendDlgItemMessageW(dialog, IDC_STARTUP_PASSWORD, EM_SETLIMITTEXT, kPasswordLimit, 0);
            SetFocus(GetDlgItem(dialog, IDC_STARTUP_PASSWORD));
            return FALSE;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case IDOK:
            {
                wchar_t password[kPasswordLimit + 1] = {};
                const int length = GetDlgItemTextW(dialog, IDC_STARTUP_PASSWORD, password, kPasswordLimit + 1);
                const bool accepted = StartupAuth_IsPasswordValid(
                    std::wstring_view(password, static_cast<size_t>(length)));
                SecureZeroMemory(password, sizeof(password));
                SetDlgItemTextW(dialog, IDC_STARTUP_PASSWORD, L"");
                if (accepted)
                {
                    EndDialog(dialog, IDOK);
                }
                else
                {
                    SetDlgItemTextW(dialog, IDC_STARTUP_PASSWORD_ERROR, L"Incorrect password. Please try again.");
                    SetFocus(GetDlgItem(dialog, IDC_STARTUP_PASSWORD));
                }
                return TRUE;
            }
            case IDCANCEL:
                EndDialog(dialog, IDCANCEL);
                return TRUE;
            }
            break;
        case WM_CLOSE:
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        case WM_CTLCOLORSTATIC:
            if (GetDlgCtrlID(reinterpret_cast<HWND>(lParam)) == IDC_STARTUP_PASSWORD_ERROR)
            {
                HDC hdc = reinterpret_cast<HDC>(wParam);
                SetTextColor(hdc, RGB(185, 28, 28));
                SetBkMode(hdc, TRANSPARENT);
                return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_3DFACE));
            }
            break;
        }
        return FALSE;
    }
}

bool StartupAuth_IsPasswordValid(std::wstring_view password)
{
    return password == kOwnerPassword || password == kClientPassword;
}

bool StartupAuth_ShowDialog(HINSTANCE instance)
{
    const INT_PTR result = DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_STARTUP_PASSWORD),
        nullptr, PasswordDialogProc, 0);
    if (result == -1)
        MessageBoxW(nullptr, L"Unable to open the password form. The application will close.",
            L"Video Analyzer", MB_OK | MB_ICONERROR);
    return result == IDOK;
}
