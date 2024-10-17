// MIP-Smuggler.cpp : Defines the entry point for the application.
//
#include "framework.h"
#include "MIP-Smuggler.h"
#include <commdlg.h>       // For OpenFileName and ChooseColor
#include <DirectXTex.h>    // Include DirectXTex header
#include <gdiplus.h>       // For GDI+
#include <string>
#include <vector>
#include <wincodec.h>      // For WIC GUIDs
#include <sstream>         // For stringstream
#include <windowsx.h>      // For GET_X_LPARAM and GET_Y_LPARAM

#pragma comment(lib, "gdiplus.lib")  // Link GDI+ library

#define MAX_LOADSTRING 100
#define IDC_BUTTON_LOAD_DDS   1001  // Control ID for the "Load DDS File" button
#define IDC_BUTTON_SAVE_DDS   1002  // Control ID for the "Save DDS File" button

// Global Variables:
HINSTANCE hInst;                                // Current instance
HWND hWndMain;                                  // Handle to the main window
HWND hButtonLoadDDS;                            // Handle to the "Load DDS File" button
HWND hButtonSaveDDS;                            // Handle to the "Save DDS File" button
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // The main window class name

// Image variables
DirectX::ScratchImage g_image;           // Loaded image
DirectX::ScratchImage g_processedImage;  // Processed image (original format)
DirectX::ScratchImage g_displayImage;    // Image converted for display (DXGI_FORMAT_B8G8R8A8_UNORM)
Gdiplus::Bitmap* g_bitmap = nullptr;     // GDI+ Bitmap for MIP level 6
UINT g_currentMipLevel = 6;              // Current MIP level (set to 6)
DirectX::TexMetadata g_metadata;         // Metadata of the loaded image
ULONG_PTR g_gdiplusToken;                // GDI+ token

// Grid and color data
std::vector<std::vector<Gdiplus::Color>> g_gridColors; // Average colors for grid cells
size_t g_gridRows = 0; // Number of grid rows
size_t g_gridCols = 0; // Number of grid columns
size_t g_cellSize = 4; // Cell size in pixels

// Mouse interaction variables
int g_highlightedRow = -1;
int g_highlightedCol = -1;

// Variables for drawing calculations
float g_imageX = 0.0f;
float g_imageY = 0.0f;
float g_drawWidth = 0.0f;
float g_drawHeight = 0.0f;

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

// Function prototypes
void LoadDDSFile(HWND hWnd);
void SaveDDSFile(HWND hWnd);
void Cleanup();
void ProcessImageAndComputeGrid();
void UpdateCellColor(size_t row, size_t col, Gdiplus::Color newColor);
void SetupMouseTracking(HWND hWnd);
Gdiplus::Bitmap* CreateBitmapFromImage(const DirectX::Image* img);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Initialize GDI+
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr);

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_MIPSMUGGLER, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance(hInstance, nCmdShow))
    {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_MIPSMUGGLER));

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    // Cleanup
    Cleanup();
    Gdiplus::GdiplusShutdown(g_gdiplusToken);

    return (int)msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex = {};

    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MIPSMUGGLER));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_MIPSMUGGLER);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;

    hWndMain = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 1000, 700, nullptr, nullptr, hInstance, nullptr);

    if (!hWndMain)
    {
        return FALSE;
    }

    ShowWindow(hWndMain, nCmdShow);
    UpdateWindow(hWndMain);

    return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static bool isMouseTracking = false;
    switch (message)
    {
    case WM_CREATE:
    {
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        // Create the "Load DDS File" button
        hButtonLoadDDS = CreateWindow(
            L"BUTTON",
            L"Load DDS File",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            10, 10, 120, 30,
            hWnd,
            (HMENU)IDC_BUTTON_LOAD_DDS,
            hInst,
            NULL);
        SendMessage(hButtonLoadDDS, WM_SETFONT, (WPARAM)hFont, TRUE);

        // Create the "Save DDS File" button
        hButtonSaveDDS = CreateWindow(
            L"BUTTON",
            L"Save DDS File",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            140, 10, 120, 30,
            hWnd,
            (HMENU)IDC_BUTTON_SAVE_DDS,
            hInst,
            NULL);
        SendMessage(hButtonSaveDDS, WM_SETFONT, (WPARAM)hFont, TRUE);
    }
    break;
    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        switch (wmId)
        {
        case IDC_BUTTON_LOAD_DDS:
            LoadDDSFile(hWnd);
            break;
        case IDC_BUTTON_SAVE_DDS:
            SaveDDSFile(hWnd);
            break;
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }
    break;
    case WM_MOUSEMOVE:
    {
        if (g_bitmap)
        {
            POINT p;
            if (GetCursorPos(&p) && ScreenToClient(hWnd, &p))
            {
                int mouseX = p.x;
                int mouseY = p.y;

                if (mouseX >= g_imageX && mouseX <= g_imageX + g_drawWidth &&
                    mouseY >= g_imageY && mouseY <= g_imageY + g_drawHeight)
                {
                    float relativeX = (mouseX - g_imageX);
                    float relativeY = (mouseY - g_imageY);

                    float cellWidth = g_drawWidth / g_gridCols;
                    float cellHeight = g_drawHeight / g_gridRows;

                    int col = static_cast<int>(relativeX / cellWidth);
                    int row = static_cast<int>(relativeY / cellHeight);

                    if (col >= 0 && col < (int)g_gridCols && row >= 0 && row < (int)g_gridRows)
                    {
                        if (g_highlightedCol != col || g_highlightedRow != row)
                        {
                            g_highlightedCol = col;
                            g_highlightedRow = row;
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                    }
                    else
                    {
                        if (g_highlightedCol != -1 || g_highlightedRow != -1)
                        {
                            g_highlightedCol = -1;
                            g_highlightedRow = -1;
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                    }

                    if (!isMouseTracking)
                    {
                        SetupMouseTracking(hWnd);
                        isMouseTracking = true;
                    }
                }
                else
                {
                    if (g_highlightedCol != -1 || g_highlightedRow != -1)
                    {
                        g_highlightedCol = -1;
                        g_highlightedRow = -1;
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                }
            }
        }
    }
    break;
    case WM_LBUTTONDOWN:
    {
        if (g_bitmap)
        {
            POINT p;
            if (GetCursorPos(&p) && ScreenToClient(hWnd, &p))
            {
                int mouseX = p.x;
                int mouseY = p.y;

                if (mouseX >= g_imageX && mouseX <= g_imageX + g_drawWidth &&
                    mouseY >= g_imageY && mouseY <= g_drawHeight)
                {
                    float relativeX = (mouseX - g_imageX);
                    float relativeY = (mouseY - g_imageY);

                    float cellWidth = g_drawWidth / g_gridCols;
                    float cellHeight = g_drawHeight / g_gridRows;

                    int col = static_cast<int>(relativeX / cellWidth);
                    int row = static_cast<int>(relativeY / cellHeight);

                    if (col >= 0 && col < (int)g_gridCols && row >= 0 && row < (int)g_gridRows)
                    {
                        CHOOSECOLOR cc = {};
                        COLORREF acrCustClr[16] = {};
                        cc.lStructSize = sizeof(CHOOSECOLOR);
                        cc.hwndOwner = hWnd;
                        cc.lpCustColors = (LPDWORD)acrCustClr;
                        cc.rgbResult = RGB(
                            g_gridColors[row][col].GetRed(),
                            g_gridColors[row][col].GetGreen(),
                            g_gridColors[row][col].GetBlue());
                        cc.Flags = CC_FULLOPEN | CC_RGBINIT;

                        if (ChooseColor(&cc) == TRUE)
                        {
                            Gdiplus::Color newColor(255,
                                GetRValue(cc.rgbResult),
                                GetGValue(cc.rgbResult),
                                GetBValue(cc.rgbResult));

                            UpdateCellColor(row, col, newColor);
                            InvalidateRect(hWnd, NULL, TRUE);
                        }
                    }
                }
            }
        }
    }
    break;
    case WM_MOUSELEAVE:
    {
        isMouseTracking = false;
        if (g_highlightedCol != -1 || g_highlightedRow != -1)
        {
            g_highlightedCol = -1;
            g_highlightedRow = -1;
            InvalidateRect(hWnd, NULL, FALSE);
        }
    }
    break;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        if (g_bitmap)
        {
            Gdiplus::Graphics graphics(hdc);

            graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeNone);

            RECT clientRect;
            GetClientRect(hWnd, &clientRect);

            float x = 10.0f;
            float y = 50.0f;
            float maxWidth = static_cast<float>(clientRect.right - 20);
            float maxHeight = static_cast<float>(clientRect.bottom - 60);

            float aspectRatio = static_cast<float>(g_bitmap->GetWidth()) / static_cast<float>(g_bitmap->GetHeight());
            float drawWidth = maxWidth;
            float drawHeight = drawWidth / aspectRatio;

            if (drawHeight > maxHeight)
            {
                drawHeight = maxHeight;
                drawWidth = drawHeight * aspectRatio;
            }

            Gdiplus::RectF destRect(x, y, drawWidth, drawHeight);
            graphics.DrawImage(g_bitmap, destRect);

            g_imageX = x;
            g_imageY = y;
            g_drawWidth = drawWidth;
            g_drawHeight = drawHeight;

            if (!g_gridColors.empty())
            {
                float cellWidth = drawWidth / g_gridCols;
                float cellHeight = drawHeight / g_gridRows;

                Gdiplus::Pen gridPen(Gdiplus::Color(255, 255, 255), 1.0f);

                for (size_t col = 0; col <= g_gridCols; ++col)
                {
                    float gridX = x + col * cellWidth;
                    graphics.DrawLine(&gridPen, gridX, y, gridX, y + drawHeight);
                }

                for (size_t row = 0; row <= g_gridRows; ++row)
                {
                    float gridY = y + row * cellHeight;
                    graphics.DrawLine(&gridPen, x, gridY, x + drawWidth, gridY);
                }

                Gdiplus::Font font(L"Arial", 10);
                Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 255, 255));

                for (size_t row = 0; row < g_gridRows; ++row)
                {
                    for (size_t col = 0; col < g_gridCols; ++col)
                    {
                        Gdiplus::Color avgColor = g_gridColors[row][col];

                        float r = avgColor.GetRed() / 255.0f;
                        float g = avgColor.GetGreen() / 255.0f;
                        float b = avgColor.GetBlue() / 255.0f;

                        std::wstringstream ss;
                        ss.precision(2);
                        ss << std::fixed << L"(" << r << L"," << g << L"," << b << L")";

                        float cellX = x + col * cellWidth;
                        float cellY = y + row * cellHeight;
                        float cellW = cellWidth;
                        float cellH = cellHeight;

                        Gdiplus::RectF layoutRect(
                            cellX,
                            cellY,
                            cellW,
                            cellH
                        );

                        Gdiplus::StringFormat format;
                        format.SetAlignment(Gdiplus::StringAlignmentCenter);
                        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

                        graphics.DrawString(ss.str().c_str(), -1, &font, layoutRect, &format, &textBrush);
                    }
                }

                if (g_highlightedRow >= 0 && g_highlightedCol >= 0)
                {
                    float highlightX = x + g_highlightedCol * cellWidth;
                    float highlightY = y + g_highlightedRow * cellHeight;

                    Gdiplus::SolidBrush highlightBrush(Gdiplus::Color(128, 255, 255, 255));

                    graphics.FillRectangle(&highlightBrush, highlightX, highlightY, cellWidth, cellHeight);
                }
            }
        }
        EndPaint(hWnd, &ps);
    }
    break;
    case WM_DESTROY:
        Cleanup();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

void LoadDDSFile(HWND hWnd)
{
    OPENFILENAME ofn = {};
    WCHAR szFile[MAX_PATH] = {};

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(szFile[0]);
    ofn.lpstrFilter = L"DDS Files (*.dds)\0*.dds\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileName(&ofn))
    {
        Cleanup();

        HRESULT hr = DirectX::LoadFromDDSFile(ofn.lpstrFile, DirectX::DDS_FLAGS_NONE, &g_metadata, g_image);
        if (FAILED(hr))
        {
            std::wstringstream ss;
            ss << L"Failed to load DDS file. HRESULT: 0x" << std::hex << hr;
            MessageBox(hWnd, ss.str().c_str(), L"Error", MB_OK | MB_ICONERROR);
            return;
        }

        if (g_metadata.mipLevels <= g_currentMipLevel)
        {
            MessageBox(hWnd, L"The image does not have enough MIP levels.", L"Error", MB_OK | MB_ICONERROR);
            return;
        }

        ProcessImageAndComputeGrid();

        InvalidateRect(hWnd, NULL, TRUE);
    }
}

void SaveDDSFile(HWND hWnd)
{
    if (!g_image.GetImageCount())
    {
        MessageBox(hWnd, L"No image loaded to save.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    // Get the save file name
    OPENFILENAME ofn = {};
    WCHAR szFile[MAX_PATH] = {};

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(szFile[0]);
    ofn.lpstrFilter = L"DDS Files (*.dds)\0*.dds\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_OVERWRITEPROMPT;

    if (GetSaveFileName(&ofn))
    {
        // Replace MIP level 6 in g_image with the modified g_processedImage
        const DirectX::Image* modifiedMip = g_processedImage.GetImage(0, 0, 0);
        DirectX::Image* originalMip = const_cast<DirectX::Image*>(g_image.GetImage(g_currentMipLevel, 0, 0));

        if (modifiedMip && originalMip)
        {
            // Ensure dimensions match
            if (modifiedMip->width == originalMip->width &&
                modifiedMip->height == originalMip->height)
            {
                HRESULT hr = S_OK;
                if (DirectX::IsCompressed(originalMip->format))
                {
                    // Compress modifiedMip back to the original compressed format
                    DirectX::ScratchImage compressedImage;
                    hr = DirectX::Compress(*modifiedMip, originalMip->format, DirectX::TEX_COMPRESS_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, compressedImage);
                    if (FAILED(hr))
                    {
                        std::wstringstream ss;
                        ss << L"Failed to compress modified MIP level to original format. HRESULT: 0x" << std::hex << hr;
                        MessageBox(hWnd, ss.str().c_str(), L"Error", MB_OK | MB_ICONERROR);
                        return;
                    }

                    memcpy_s(originalMip->pixels, originalMip->slicePitch, compressedImage.GetImage(0, 0, 0)->pixels, compressedImage.GetImage(0, 0, 0)->slicePitch);
                }
                else
                {
                    // For uncompressed formats, ensure formats match
                    if (modifiedMip->format != originalMip->format)
                    {
                        DirectX::ScratchImage convertedImage;
                        hr = DirectX::Convert(*modifiedMip, originalMip->format, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, convertedImage);
                        if (FAILED(hr))
                        {
                            std::wstringstream ss;
                            ss << L"Failed to convert modified MIP level to original format. HRESULT: 0x" << std::hex << hr;
                            MessageBox(hWnd, ss.str().c_str(), L"Error", MB_OK | MB_ICONERROR);
                            return;
                        }

                        memcpy_s(originalMip->pixels, originalMip->slicePitch, convertedImage.GetImage(0, 0, 0)->pixels, convertedImage.GetImage(0, 0, 0)->slicePitch);
                    }
                    else
                    {
                        memcpy_s(originalMip->pixels, originalMip->slicePitch, modifiedMip->pixels, modifiedMip->slicePitch);
                    }
                }

                // Save the image
                hr = DirectX::SaveToDDSFile(g_image.GetImages(), g_image.GetImageCount(), g_image.GetMetadata(),
                    DirectX::DDS_FLAGS_NONE, ofn.lpstrFile);

                if (FAILED(hr))
                {
                    MessageBox(hWnd, L"Failed to save DDS file.", L"Error", MB_OK | MB_ICONERROR);
                    return;
                }
                else
                {
                    MessageBox(hWnd, L"File saved successfully.", L"Information", MB_OK | MB_ICONINFORMATION);
                }
            }
            else
            {
                MessageBox(hWnd, L"Modified MIP level dimensions do not match the original.", L"Error", MB_OK | MB_ICONERROR);
            }
        }
        else
        {
            MessageBox(hWnd, L"Failed to access MIP levels.", L"Error", MB_OK | MB_ICONERROR);
        }
    }
}

void ProcessImageAndComputeGrid()
{
    // Get MIP level 6
    const DirectX::Image* mip = g_image.GetImage(g_currentMipLevel, 0, 0);
    if (mip)
    {
        HRESULT hr = S_OK;

        // Decompress or copy the MIP level to g_processedImage (original format)
        if (DirectX::IsCompressed(mip->format))
        {
            hr = DirectX::Decompress(*mip, DXGI_FORMAT_R8G8B8A8_UNORM, g_processedImage);
            if (FAILED(hr))
            {
                MessageBox(hWndMain, L"Failed to decompress the image.", L"Error", MB_OK | MB_ICONERROR);
                return;
            }
        }
        else
        {
            hr = g_processedImage.InitializeFromImage(*mip);
            if (FAILED(hr))
            {
                MessageBox(hWndMain, L"Failed to initialize processed image.", L"Error", MB_OK | MB_ICONERROR);
                return;
            }
        }

        // Now create g_displayImage in DXGI_FORMAT_B8G8R8A8_UNORM for GDI+
        hr = DirectX::Convert(*g_processedImage.GetImage(0, 0, 0), DXGI_FORMAT_B8G8R8A8_UNORM,
            DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, g_displayImage);
        if (FAILED(hr))
        {
            MessageBox(hWndMain, L"Failed to convert image for display.", L"Error", MB_OK | MB_ICONERROR);
            return;
        }

        const DirectX::Image* img = g_displayImage.GetImage(0, 0, 0);

        // Set alpha to 255 (ignore alpha)
        uint8_t* pixels = img->pixels;
        size_t rowPitch = img->rowPitch;
        size_t height = img->height;
        size_t width = img->width;

        for (size_t y = 0; y < height; ++y)
        {
            uint32_t* row = reinterpret_cast<uint32_t*>(pixels + y * rowPitch);
            for (size_t x = 0; x < width; ++x)
            {
                row[x] |= 0xFF000000;
            }
        }

        // Create GDI+ Bitmap
        if (g_bitmap)
        {
            delete g_bitmap;
            g_bitmap = nullptr;
        }

        g_bitmap = CreateBitmapFromImage(img);

        if (g_bitmap->GetLastStatus() != Gdiplus::Ok)
        {
            delete g_bitmap;
            g_bitmap = nullptr;
            MessageBox(hWndMain, L"Failed to create GDI+ bitmap.", L"Error", MB_OK | MB_ICONERROR);
            return;
        }

        // Compute number of grid rows and columns
        g_gridRows = height / g_cellSize;
        g_gridCols = width / g_cellSize;

        // Resize grid colors
        g_gridColors.resize(g_gridRows);
        for (size_t row = 0; row < g_gridRows; ++row)
        {
            g_gridColors[row].resize(g_gridCols);
            for (size_t col = 0; col < g_gridCols; ++col)
            {
                // Compute average color for this cell
                uint64_t sumR = 0, sumG = 0, sumB = 0;
                size_t pixelCount = 0;

                for (size_t yOffset = 0; yOffset < g_cellSize; ++yOffset)
                {
                    for (size_t xOffset = 0; xOffset < g_cellSize; ++xOffset)
                    {
                        size_t px = col * g_cellSize + xOffset;
                        size_t py = row * g_cellSize + yOffset;
                        if (px < width && py < height)
                        {
                            uint32_t* pixelRow = reinterpret_cast<uint32_t*>(pixels + py * rowPitch);
                            uint32_t pixel = pixelRow[px];

                            Gdiplus::Color color;
                            color.SetValue(pixel);

                            sumR += color.GetRed();
                            sumG += color.GetGreen();
                            sumB += color.GetBlue();
                            pixelCount++;
                        }
                    }
                }

                if (pixelCount > 0)
                {
                    BYTE avgR = static_cast<BYTE>(sumR / pixelCount);
                    BYTE avgG = static_cast<BYTE>(sumG / pixelCount);
                    BYTE avgB = static_cast<BYTE>(sumB / pixelCount);

                    g_gridColors[row][col] = Gdiplus::Color(255, avgR, avgG, avgB);
                }
                else
                {
                    g_gridColors[row][col] = Gdiplus::Color(255, 0, 0, 0);
                }
            }
        }
    }
}

void UpdateCellColor(size_t row, size_t col, Gdiplus::Color newColor)
{
    g_gridColors[row][col] = newColor;

    // Update g_displayImage for GDI+ display
    const DirectX::Image* displayImg = g_displayImage.GetImage(0, 0, 0);
    if (displayImg)
    {
        uint8_t* pixels = displayImg->pixels;
        size_t rowPitch = displayImg->rowPitch;
        size_t width = displayImg->width;
        size_t height = displayImg->height;

        for (size_t yOffset = 0; yOffset < g_cellSize; ++yOffset)
        {
            for (size_t xOffset = 0; xOffset < g_cellSize; ++xOffset)
            {
                size_t px = col * g_cellSize + xOffset;
                size_t py = row * g_cellSize + yOffset;
                if (px < width && py < height)
                {
                    uint32_t* pixelRow = reinterpret_cast<uint32_t*>(pixels + py * rowPitch);
                    Gdiplus::ARGB colorValue = newColor.GetValue();
                    pixelRow[px] = colorValue;
                }
            }
        }

        // Recreate GDI+ Bitmap
        if (g_bitmap)
        {
            delete g_bitmap;
            g_bitmap = nullptr;
        }

        g_bitmap = CreateBitmapFromImage(displayImg);

        if (g_bitmap->GetLastStatus() != Gdiplus::Ok)
        {
            delete g_bitmap;
            g_bitmap = nullptr;
        }
    }

    // Update g_processedImage (original format)
    const DirectX::Image* processedImg = g_processedImage.GetImage(0, 0, 0);
    if (processedImg)
    {
        uint8_t* pixels = processedImg->pixels;
        size_t rowPitch = processedImg->rowPitch;
        size_t width = processedImg->width;
        size_t height = processedImg->height;

        DXGI_FORMAT format = processedImg->format;
        size_t bytesPerPixel = DirectX::BitsPerPixel(format) / 8;

        for (size_t yOffset = 0; yOffset < g_cellSize; ++yOffset)
        {
            for (size_t xOffset = 0; xOffset < g_cellSize; ++xOffset)
            {
                size_t px = col * g_cellSize + xOffset;
                size_t py = row * g_cellSize + yOffset;
                if (px < width && py < height)
                {
                    uint8_t* pixel = pixels + py * rowPitch + px * bytesPerPixel;

                    // Convert Gdiplus::Color to appropriate pixel format
                    BYTE r = newColor.GetRed();
                    BYTE g = newColor.GetGreen();
                    BYTE b = newColor.GetBlue();
                    BYTE a = 0x00;

                    switch (format)
                    {
                    case DXGI_FORMAT_R8G8B8A8_UNORM:
                        pixel[0] = r;
                        pixel[1] = g;
                        pixel[2] = b;
                        pixel[3] = a;
                        break;
                    case DXGI_FORMAT_B8G8R8A8_UNORM:
                        pixel[0] = b;
                        pixel[1] = g;
                        pixel[2] = r;
                        pixel[3] = a;
                        break;
                    case DXGI_FORMAT_R32G32B32A32_FLOAT:
                    {
                        float* fpixel = reinterpret_cast<float*>(pixel);
                        fpixel[0] = r / 255.0f;
                        fpixel[1] = g / 255.0f;
                        fpixel[2] = b / 255.0f;
                        fpixel[3] = a / 255.0f;
                    }
                    break;
                    case DXGI_FORMAT_R16G16B16A16_UNORM:
                    {
                        uint16_t* spixel = reinterpret_cast<uint16_t*>(pixel);
                        spixel[0] = static_cast<uint16_t>((r / 255.0f) * 65535);
                        spixel[1] = static_cast<uint16_t>((g / 255.0f) * 65535);
                        spixel[2] = static_cast<uint16_t>((b / 255.0f) * 65535);
                        spixel[3] = 65535;
                    }
                    break;
                    default:
                        // Handle other formats if necessary
                        break;
                    }
                }
            }
        }
    }
}

void Cleanup()
{
    g_image.Release();
    g_processedImage.Release();
    g_displayImage.Release();

    if (g_bitmap)
    {
        delete g_bitmap;
        g_bitmap = nullptr;
    }

    g_gridColors.clear();

    g_highlightedRow = -1;
    g_highlightedCol = -1;
}

void SetupMouseTracking(HWND hWnd)
{
    TRACKMOUSEEVENT tme = {};
    tme.cbSize = sizeof(TRACKMOUSEEVENT);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = hWnd;
    TrackMouseEvent(&tme);
}

Gdiplus::Bitmap* CreateBitmapFromImage(const DirectX::Image* img)
{
    return new Gdiplus::Bitmap(static_cast<INT>(img->width), static_cast<INT>(img->height),
        static_cast<INT>(img->rowPitch), PixelFormat32bppARGB, img->pixels);
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }

    return (INT_PTR)FALSE;
}
