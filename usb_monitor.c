#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <tchar.h>
#include <dbt.h>
#include <setupapi.h>
#include <initguid.h>
#include <shellapi.h>  // Added for system tray
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "setupapi.lib")
#define UNICODE 1
#define _UNICODE 1

// USB Device Interface GUID
static const GUID GUID_DEVINTERFACE_USB_DEVICE = 
    { 0xA5DCBF10, 0x6530, 0x11D2, { 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED } };

// Tray defines
#define ID_TRAY_APP_ICON 5000
#define ID_TRAY_EXIT_CONTEXT_MENU_ITEM 3000
#define WM_TRAYICON (WM_USER + 1)
UINT WM_TASKBARCREATED = 0;
NOTIFYICONDATA g_notifyIconData = {0};
HMENU g_menu = NULL;

#else
#include <libudev.h>
#include <unistd.h>
#include <poll.h>
#include <sys/types.h>
#include <dirent.h>
#endif

// Function to get the directory of the executable
char* get_exe_dir() {
    char* dir = (char*)malloc(PATH_MAX);
    if (!dir) return NULL;

#ifdef _WIN32
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char* last_slash = strrchr(path, '\\');
    if (last_slash) *last_slash = '\0';
    strcpy(dir, path);
#else
    char path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, PATH_MAX - 1);
    if (len != -1) {
        path[len] = '\0';
        char* last_slash = strrchr(path, '/');
        if (last_slash) *last_slash = '\0';
        strcpy(dir, path);
    } else {
        dir[0] = '\0';
    }
#endif
    return dir;
}

// Function to play a random sound
void play_sound() {
    char* exe_dir = get_exe_dir();
    if (!exe_dir) return;

    char wav_files[100][MAX_PATH];
    int count = 0;

#ifdef _WIN32
    WIN32_FIND_DATAA findData;
    HANDLE hFind;
    char search_path[MAX_PATH];
    snprintf(search_path, MAX_PATH, "%s\\*.wav", exe_dir);
    hFind = FindFirstFileA(search_path, &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && count < 100) {
                strcpy(wav_files[count], findData.cFileName);
                count++;
            }
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }
#else
    DIR *d = opendir(exe_dir);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL && count < 100) {
            if (ent->d_type == DT_REG) {
                const char *ext = strrchr(ent->d_name, '.');
                if (ext && strcmp(ext, ".wav") == 0) {
                    strcpy(wav_files[count], ent->d_name);
                    count++;
                }
            }
        }
        closedir(d);
    }
#endif

    if (count > 0) {
        static int seeded = 0;
        if (!seeded) {
            srand((unsigned int)time(NULL));
            seeded = 1;
        }
        int idx = rand() % count;

        char sound_file[PATH_MAX];
        snprintf(sound_file, PATH_MAX, "%s/%s", exe_dir, wav_files[idx]);

#ifdef _WIN32
        PlaySoundA(sound_file, NULL, SND_FILENAME | SND_ASYNC);
#else
        char cmd[PATH_MAX + 20];
        snprintf(cmd, sizeof(cmd), "aplay \"%s\" &", sound_file);
        system(cmd);
#endif
    }

    free(exe_dir);
}

#ifdef _WIN32
// Initialize the NOTIFYICONDATA structure
void InitNotifyIconData(HWND hWnd) {
    memset(&g_notifyIconData, 0, sizeof(NOTIFYICONDATA));
    g_notifyIconData.cbSize = sizeof(NOTIFYICONDATA);
    g_notifyIconData.hWnd = hWnd;
    g_notifyIconData.uID = ID_TRAY_APP_ICON;
    g_notifyIconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_notifyIconData.uCallbackMessage = WM_TRAYICON;
    g_notifyIconData.hIcon = LoadIcon(NULL, IDI_APPLICATION);  // Default icon
    strcpy(g_notifyIconData.szTip, "USB Monitor");
}

// Register for device notifications
BOOL DoRegisterDeviceInterfaceToHwnd(GUID InterfaceClassGuid, HWND hWnd, HDEVNOTIFY *hDeviceNotify) {
    DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;
    ZeroMemory(&NotificationFilter, sizeof(NotificationFilter));
    NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    NotificationFilter.dbcc_classguid = InterfaceClassGuid;

    *hDeviceNotify = RegisterDeviceNotification(hWnd, &NotificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);
    return (*hDeviceNotify != NULL);
}

// Window procedure
LRESULT CALLBACK WinProcCallback(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static HDEVNOTIFY hDeviceNotify = NULL;

    if (message == WM_TASKBARCREATED) {
        Shell_NotifyIcon(NIM_ADD, &g_notifyIconData);
        return 0;
    }

    switch (message) {
    case WM_CREATE:
        if (!DoRegisterDeviceInterfaceToHwnd(GUID_DEVINTERFACE_USB_DEVICE, hWnd, &hDeviceNotify)) {
            return -1;
        }
        g_menu = CreatePopupMenu();
        AppendMenu(g_menu, MF_STRING, ID_TRAY_EXIT_CONTEXT_MENU_ITEM, TEXT("Exit"));
        break;

    case WM_DEVICECHANGE: {
        if (wParam == DBT_DEVICEARRIVAL) {
            PDEV_BROADCAST_HDR lpdb = (PDEV_BROADCAST_HDR)lParam;
            if (lpdb->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
                PDEV_BROADCAST_DEVICEINTERFACE pdbdi = (PDEV_BROADCAST_DEVICEINTERFACE)lpdb;

                // Check if it's a mass storage device
                BOOL is_mass_storage = FALSE;
                HDEVINFO hDevInfo = SetupDiCreateDeviceInfoList(&pdbdi->dbcc_classguid, NULL);
                if (hDevInfo != INVALID_HANDLE_VALUE) {
                    SP_DEVICE_INTERFACE_DATA interfaceData = {0};
                    interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
                    if (SetupDiOpenDeviceInterface(hDevInfo, pdbdi->dbcc_name, 0, &interfaceData)) {
                        DWORD reqSize = 0;
                        SP_DEVINFO_DATA devInfoData = {0};
                        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
                        SetupDiGetDeviceInterfaceDetail(hDevInfo, &interfaceData, NULL, 0, &reqSize, &devInfoData);

                        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && reqSize > 0) {
                            PSP_DEVICE_INTERFACE_DETAIL_DATA detailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(reqSize);
                            if (detailData) {
                                detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
                                if (SetupDiGetDeviceInterfaceDetail(hDevInfo, &interfaceData, detailData, reqSize, NULL, &devInfoData)) {
                                    BYTE buffer[1024] = {0};
                                    DWORD size = 0;
                                    DWORD type;
                                    if (SetupDiGetDeviceRegistryProperty(hDevInfo, &devInfoData, SPDRP_COMPATIBLEIDS, &type, buffer, sizeof(buffer), &size)) {
                                        char* ids = (char*)buffer;
                                        while (*ids) {
                                            if (strstr(ids, "USB\\Class_08")) {
                                                is_mass_storage = TRUE;
                                                break;
                                            }
                                            ids += strlen(ids) + 1;
                                        }
                                    }
                                }
                                free(detailData);
                            }
                        }
                    }
                    SetupDiDestroyDeviceInfoList(hDevInfo);
                }

                if (!is_mass_storage) {
                    play_sound();
                }
            }
        }
        break;
    }

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONDOWN) {
            POINT curPoint;
            GetCursorPos(&curPoint);
            SetForegroundWindow(hWnd);
            UINT clicked = TrackPopupMenu(g_menu, TPM_RETURNCMD | TPM_NONOTIFY, curPoint.x, curPoint.y, 0, hWnd, NULL);
            if (clicked == ID_TRAY_EXIT_CONTEXT_MENU_ITEM) {
                PostQuitMessage(0);
            }
        }
        break;

    case WM_DESTROY:
        if (hDeviceNotify) UnregisterDeviceNotification(hDeviceNotify);
        Shell_NotifyIcon(NIM_DELETE, &g_notifyIconData);
        if (g_menu) DestroyMenu(g_menu);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Window class registration
BOOL InitWindowClass() {
    WNDCLASSEX wndClass = {0};
    wndClass.cbSize = sizeof(WNDCLASSEX);
    wndClass.style = CS_HREDRAW | CS_VREDRAW;
    wndClass.lpfnWndProc = WinProcCallback;
    wndClass.hInstance = GetModuleHandle(NULL);
    wndClass.lpszClassName = TEXT("USBMonitorClass");
    wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wndClass.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    return RegisterClassEx(&wndClass);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WM_TASKBARCREATED = RegisterWindowMessageA("TaskbarCreated");

    if (!InitWindowClass()) return -1;

    HWND hWnd = CreateWindow(TEXT("USBMonitorClass"), TEXT("USB Monitor"), 
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 
                             300, 200, NULL, NULL, hInstance, NULL);
    if (!hWnd) return -1;

    InitNotifyIconData(hWnd);
    Shell_NotifyIcon(NIM_ADD, &g_notifyIconData);

    // Hide the window
    ShowWindow(hWnd, SW_HIDE);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

#else
// Linux main (unchanged)
int main() {
    struct udev *udev = udev_new();
    if (!udev) {
        fprintf(stderr, "Failed to create udev\n");
        return 1;
    }

    struct udev_monitor *monitor = udev_monitor_new_from_netlink(udev, "udev");
    if (!monitor) {
        fprintf(stderr, "Failed to create monitor\n");
        udev_unref(udev);
        return 1;
    }

    udev_monitor_filter_add_match_subsystem_devtype(monitor, "usb", NULL);
    udev_monitor_enable_receiving(monitor);

    int fd = udev_monitor_get_fd(monitor);
    if (fd < 0) {
        fprintf(stderr, "Failed to get monitor fd\n");
        udev_monitor_unref(monitor);
        udev_unref(udev);
        return 1;
    }

    while (1) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        if (poll(&pfd, 1, -1) > 0 && (pfd.revents & POLLIN)) {
            struct udev_device *device = udev_monitor_receive_device(monitor);
            if (device) {
                const char *action = udev_device_get_action(device);
                if (action && strcmp(action, "add") == 0) {
                    play_sound();
                }
                udev_device_unref(device);
            }
        }
    }

    udev_monitor_unref(monitor);
    udev_unref(udev);
    return 0;
}
#endif