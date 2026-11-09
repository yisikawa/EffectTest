//======================================================================
//  App.cpp : EffectTest 本体（Win32 + DX11）
//
//  AltanaView の effect motion（タイムライン + 2 段 VM）だけを可視化する。
//  メッシュ/スケルトンは扱わない（ModelTest の担当領域）。
//======================================================================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <objbase.h>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

#include "Dat.h"
#include "Effect.h"
#include "EffectBase.h"
#include "Gfx.h"
#include "ListDb.h"

using namespace DirectX;

//----------------------------------------------------------------------
// グローバル状態
//----------------------------------------------------------------------
namespace
{
    const wchar_t* kWndClass = L"EffectTestWindow";
    const int      kInitW    = 1280;
    const int      kInitH    = 720;

    HWND  g_hWnd    = nullptr;
    bool  g_Running = true;

    DatFile       g_Dat;
    EffectContext g_Effect;
    std::string   g_DatPath;

    bool  g_Paused    = false;
    bool  g_Layout    = true;     // 位置 0 のインスタンスを横並びにする（確認用）
    bool  g_Trace     = false;
    bool  g_NoTexture = false;    // W: テクスチャを使わず白で描く（デバッグ）
    bool  g_ShowAxes  = false;    // X: 座標軸の表示（既定 OFF。起動時は何も描かない）
    float g_SizeScale = 1.0f;     // ビルボードの基準半径
    int   g_TexIndex  = 0;

    // カメラ（注視点原点のオービット）
    //  AltanaView のエフェクトは -Z 方向へ進むため、既定は +Z 側から見る
    float g_Yaw   = 3.14159265f;
    float g_Pitch = 0.35f;
    float g_Dist  = 14.0f;
    bool  g_Drag  = false;
    POINT g_LastMouse = {};

    FILE* g_TraceFile = nullptr;
    float g_Dt        = 1.0f / 60.0f;

    //--------------------------------------------------------------
    // List ブラウザ
    //--------------------------------------------------------------
    ListDb   g_List;
    char     g_FfxiBase[MAX_PATH] = {};
    HWND     g_hPanel         = nullptr;
    HWND     g_hGroup         = nullptr;
    HWND     g_hItems         = nullptr;
    HWND     g_hSchedule      = nullptr;
    HWND     g_hScheduleLabel = nullptr;

    EffectContext            g_ActionCtx;          // NPC の 0x07 用
    bool                     g_ActionActive = false;
    std::vector<std::string> g_ScheduleNames;

    int  g_PendingItem    = -1;    // 確定待ちの項目 index
    int  g_PendingFrames  = 0;     // 静止コマ数
    int  g_CommittedItem  = -1;
    int  g_CommittedGroup = -1;
    bool g_CommitBusy     = false;

    const int kCommitDelayFrames = 7;   // 60 fps で 116.7 ms（設計 §4.7.1 D4）

    EffectBase               g_Base;               // ベースエフェクト（ROM\0\0.DAT）

    const wchar_t* kPanelClass = L"EffectTestPanel";

    // コントロール ID
    enum : int { IDC_GROUP = 1001, IDC_ITEMS = 1002, IDC_SCHEDULE = 1003, IDC_SCHEDULE_LABEL = 1004 };
}

static bool LoadDatFile(const char* path);
static void UpdateTitle();
static void UpdateCamera(int w, int h);
static void RenderScene(bool present = true);
static void HandleKey(WPARAM key, bool down);
static void OpenFileDialog();

//--------------------------------------------------------------
// List ブラウザ（パネルウィンドウ）
//--------------------------------------------------------------
static void BuildPanelControls(HWND hPanel);
static void FillGroups();
static void FillItems();
static void CommitSelection(int itemIndex);
static void PlaySelectedSchedule(int index);
static void TickPendingCommit();
static void ScheduleClear();
static void ClearSelection();
static void UpdateScheduleLabel(int count);
static LRESULT CALLBACK PanelProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);

//----------------------------------------------------------------------
// ログ出力（effecttest_log.txt に追記 + デバッガ）
//----------------------------------------------------------------------
static void Log(const char* fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsprintf_s(buf, fmt, ap);
    va_end(ap);

    OutputDebugStringA(buf);
    OutputDebugStringA("\n");

    FILE* fp = nullptr;
    if (fopen_s(&fp, "effecttest_log.txt", "ab") == 0 && fp)
    {
        fprintf(fp, "%s\n", buf);
        fclose(fp);
    }
}

//----------------------------------------------------------------------
// FFXI インストール先の検出（レジストリ → 既知パス）
//----------------------------------------------------------------------
static bool FindFFXI(char* out, size_t outSize)
{
    const char* keys[] = {
        "SOFTWARE\\WOW6432Node\\PlayOnlineUS\\InstallRoot",
        "SOFTWARE\\PlayOnlineUS\\InstallRoot",
        "SOFTWARE\\WOW6432Node\\PlayOnline\\InstallRoot",
        "SOFTWARE\\PlayOnline\\InstallRoot",
    };

    for (const char* k : keys)
    {
        HKEY hKey = nullptr;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, k, 0, KEY_READ, &hKey) != ERROR_SUCCESS) continue;

        char val[MAX_PATH] = {};
        DWORD cb = sizeof(val);
        DWORD type = 0;
        const LONG r = RegQueryValueExA(hKey, "InstallPath", nullptr, &type, (LPBYTE)val, &cb);
        RegCloseKey(hKey);
        if (r != ERROR_SUCCESS || val[0] == '\0') continue;

        char full[MAX_PATH] = {};
        sprintf_s(full, "%s%sFINAL FANTASY XI", val,
                  (val[strlen(val) - 1] == '\\') ? "" : "\\");
        if (GetFileAttributesA(full) != INVALID_FILE_ATTRIBUTES)
        {
            strncpy_s(out, outSize, full, _TRUNCATE);
            return true;
        }
    }

    const char* known[] = {
        "C:\\Program Files (x86)\\PlayOnline\\SquareEnix\\FINAL FANTASY XI",
        "C:\\Program Files\\PlayOnline\\SquareEnix\\FINAL FANTASY XI",
    };
    for (const char* p : known)
    {
        if (GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES)
        {
            strncpy_s(out, outSize, p, _TRUNCATE);
            return true;
        }
    }
    return false;
}

//----------------------------------------------------------------------
// 実行ファイルのフォルダ
//----------------------------------------------------------------------
static std::string ExeDir()
{
    char p[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, p, MAX_PATH);
    std::string s = p;
    const size_t k = s.find_last_of("\\/");
    return (k == std::string::npos) ? std::string(".") : s.substr(0, k);
}

//----------------------------------------------------------------------
// List フォルダの探索（exe の上位 / カレント）
//----------------------------------------------------------------------
static bool FindListDir(char* out, size_t outSize)
{
    const std::string exe = ExeDir();
    const char* rel[] = { "\\List", "\\..\\..\\List", "\\..\\..\\..\\List", "\\..\\List" };

    for (const char* r : rel)
    {
        char cand[MAX_PATH] = {};
        sprintf_s(cand, "%s%s", exe.c_str(), r);

        char full[MAX_PATH] = {};
        if (GetFullPathNameA(cand, MAX_PATH, full, nullptr) == 0) continue;

        char probe[MAX_PATH] = {};
        sprintf_s(probe, "%s\\Effect\\index.csv", full);
        if (GetFileAttributesA(probe) != INVALID_FILE_ATTRIBUTES)
        {
            strncpy_s(out, outSize, full, _TRUNCATE);
            return true;
        }
    }

    // カレントディレクトリ
    char cwd[MAX_PATH] = {};
    GetCurrentDirectoryA(MAX_PATH, cwd);
    char probe[MAX_PATH] = {};
    sprintf_s(probe, "%s\\List\\Effect\\index.csv", cwd);
    if (GetFileAttributesA(probe) != INVALID_FILE_ATTRIBUTES)
    {
        sprintf_s(out, outSize, "%s\\List", cwd);
        return true;
    }
    return false;
}

//----------------------------------------------------------------------
// 起動時に既定エフェクトを読み込む（引数なし起動でも何か表示する）
//----------------------------------------------------------------------
static bool LoadDefaultEffect()
{
    char base[MAX_PATH] = {};
    if (!FindFFXI(base, sizeof(base)))
    {
        Log("[auto] FFXI installation not found.");
        return false;
    }
    Log("[auto] FFXI = %s", base);

    // Fire 系の効果から順に試す（無ければ 11/17 系の近傍）
    const char* cand[] = {
        "ROM\\11\\17.DAT", "ROM\\11\\18.DAT", "ROM\\11\\19.DAT",
        "ROM\\11\\20.DAT", "ROM\\11\\21.DAT",
    };
    for (const char* c : cand)
    {
        char full[MAX_PATH] = {};
        sprintf_s(full, "%s\\%s", base, c);
        if (GetFileAttributesA(full) == INVALID_FILE_ATTRIBUTES) continue;

        if (LoadDatFile(full))
        {
            Log("[auto] loaded %s", full);
            return true;
        }
    }
    Log("[auto] no default effect DAT found.");
    return false;
}

//----------------------------------------------------------------------
// 座標軸の描画（描画が生きていることの確認用）
//----------------------------------------------------------------------
static void DrawReference()
{
    ID3D11ShaderResourceView* white = g_Effect.TextureSrv(-1);   // 無ければ Gfx 側で白

    const float o[3] = { 0.0f, 0.0f, 0.0f };

    const float axR[3] = { 2.0f, 0.0f, 0.0f };
    const float axU[3] = { 0.0f, 0.02f, 0.0f };
    const float colX[4] = { 0.9f, 0.25f, 0.25f, 1.0f };
    GfxDrawQuad(o, axR, axU, colX, white);

    const float azR[3] = { 0.0f, 0.0f, 2.0f };
    const float colZ[4] = { 0.3f, 0.45f, 1.0f, 1.0f };
    GfxDrawQuad(o, azR, axU, colZ, white);

    const float ayR[3] = { 0.02f, 0.0f, 0.0f };
    const float ayU[3] = { 0.0f, 2.0f, 0.0f };
    const float colY[4] = { 0.3f, 1.0f, 0.35f, 1.0f };
    GfxDrawQuad(o, ayR, ayU, colY, white);
}

//----------------------------------------------------------------------
// DAT 読み込み
//----------------------------------------------------------------------
static bool LoadDatFile(const char* path)
{
    g_Effect.ReleaseTextures();
    if (!g_Dat.Load(path)) return false;

    g_Effect.Build(g_Dat);
    g_Effect.BuildTextures(GfxDevice());
    g_DatPath  = path;
    g_TexIndex = 0;

    char title[512];
    sprintf_s(title, "EffectTest  -  %s  (chunks=%zu  timeline=%zu  generators=%zu  tex=%zu)\n",
              path, g_Dat.Chunks().size(), g_Effect.Timeline().size(),
              g_Effect.Generators().size(), g_Effect.Textures().size());
    OutputDebugStringA(title);
    return true;
}

//----------------------------------------------------------------------
// ステータス
//----------------------------------------------------------------------
static void UpdateTitle()
{
    if (g_hWnd == nullptr) return;

    // 起動直後（未選択）は案内のみ。表示は List の項目選択がトリガー。
    if (g_DatPath.empty())
    {
        SetWindowTextA(g_hWnd,
                       "EffectTest  —  List から項目を選択してください"
                       "   [B]list  [O]open  [X]axes  [M]blend  [Space]pause");
        return;
    }

    char buf[768];
    const char* name = g_DatPath.empty() ? "(no data)" : g_DatPath.c_str();
    sprintf_s(buf,
              "EffectTest  %s  |  ip=%zu/%zu  t=%.3f/%.3fs  inst=%zu  blend=%s  %s%s  [O]open [Space]pause [</>]step",
              name,
              g_Effect.Ip(), g_Effect.Timeline().size(),
              g_Effect.Frame() / 60.0f, g_Effect.Duration(),
              g_Effect.Instances().size(),
              GfxBlendModeName(GfxGetBlendMode()),
              g_Paused ? "PAUSED " : "",
              g_Trace ? "TRACE " : "");
    SetWindowTextA(g_hWnd, buf);
}

//----------------------------------------------------------------------
// カメラ
//----------------------------------------------------------------------
static void UpdateCamera(int w, int h)
{
    if (w <= 0 || h <= 0) return;

    const XMVECTOR eye = XMVectorSet(
        g_Dist * cosf(g_Pitch) * sinf(g_Yaw),
        g_Dist * sinf(g_Pitch),
        -g_Dist * cosf(g_Pitch) * cosf(g_Yaw), 1.0f);
    const XMMATRIX view = XMMatrixLookAtLH(eye, XMVectorSet(0, 0, 0, 1), XMVectorSet(0, 1, 0, 0));
    const XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(45.0f), (float)w / (float)h, 0.1f, 1000.0f);
    GfxSetCamera(view, proj);
}

//----------------------------------------------------------------------
// 描画
//----------------------------------------------------------------------
static void RenderScene(bool present)
{
    RECT rc = {};
    GetClientRect(g_hWnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    UpdateCamera(w, h);

    const float clear[4] = { 0.05f, 0.06f, 0.09f, 1.0f };
    GfxBeginFrame(clear);

    if (g_ShowAxes) DrawReference();   // 既定は非表示（起動時は何も描かない）

    // カメラの右/上ベクトル（ビルボード用）
    const float cy = cosf(g_Yaw), sy = sinf(g_Yaw), cp = cosf(g_Pitch), sp = sinf(g_Pitch);
    const float right[3] = { cy, 0.0f, sy };
    const float up[3]    = { -sy * sp, cp, cy * sp };

    const auto& insts = g_Effect.Instances();
    const int   count = (int)insts.size();

    for (int i = 0; i < count; ++i)
    {
        const MotionInstance& mi = insts[i];
        const float* p = mi.sub.At(SUB_POS);
        const float* s = mi.sub.At(SUB_SCALE);
        const float* c = mi.sub.At(SUB_COLOR);

        float center[3] = { p[0], p[1], p[2] };

        // 位置が未設定のエフェクトでも形を確認できるよう横並びに配置
        if (g_Layout && center[0] == 0.0f && center[1] == 0.0f && center[2] == 0.0f)
            center[0] = (i - (count - 1) * 0.5f) * 1.6f;

        const float sx  = (s[0] > 0.001f) ? s[0] : 1.0f;
        const float sy2 = (s[1] > 0.001f) ? s[1] : 1.0f;

        const float halfW = g_SizeScale * sx;
        const float halfH = g_SizeScale * sy2;

        const float axisR[3] = { right[0] * halfW, right[1] * halfW, right[2] * halfW };
        const float axisU[3] = { up[0] * halfH,    up[1] * halfH,    up[2] * halfH };

        float rgba[4] = { c[0], c[1], c[2], c[3] };
        if (rgba[0] == 0.0f && rgba[1] == 0.0f && rgba[2] == 0.0f)
        {
            rgba[0] = rgba[1] = rgba[2] = 1.0f;      // VM が色を書いていない場合の代替
        }
        if (rgba[3] <= 0.0f) rgba[3] = 1.0f;

        const int texIdx = g_NoTexture ? -1
                                       : ((mi.texIndex >= 0) ? mi.texIndex : g_TexIndex);

        // base の 0x20 を借用している場合はそちらを優先（未解決なら従来どおり白）
        ID3D11ShaderResourceView* srv = g_NoTexture ? nullptr : mi.srv;
        if (!g_NoTexture && srv == nullptr) srv = g_Effect.TextureSrv(texIdx);

        GfxDrawQuad(center, axisR, axisU, rgba, mi.uv, srv);

        if (g_TraceFile != nullptr)
        {
            fprintf(g_TraceFile,
                    "%u,%d,%s,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.4f,%.4f,%.4f,%.4f\n",
                    g_Effect.Frame(), i, mi.gen->chunk.name,
                    mi.sub.Elapsed(), mi.sub.T01(),
                    p[0], p[1], p[2], s[0], s[1], s[2],
                    c[0], c[1], c[2], c[3]);
        }
    }

    if (present) GfxEndFrame();
}

//----------------------------------------------------------------------
// ファイル選択
//----------------------------------------------------------------------
static void OpenFileDialog()
{
    char path[MAX_PATH] = {};
    if (!g_DatPath.empty()) strncpy_s(path, g_DatPath.c_str(), _TRUNCATE);

    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_hWnd;
    ofn.lpstrFilter = "FFXI DAT (*.DAT)\0*.DAT\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameA(&ofn))
    {
        LoadDatFile(path);
        g_Effect.Reset();
        UpdateTitle();
    }
}

//----------------------------------------------------------------------
// 入力
//----------------------------------------------------------------------
static void HandleKey(WPARAM key, bool down)
{
    if (!down) return;

    switch (key)
    {
    case 'O': OpenFileDialog(); break;

    case VK_SPACE: g_Paused = !g_Paused; break;

    case VK_LEFT:                                    // 1 フレーム戻す（先頭から再計算）
    {
        g_Paused = true;
        const uint32_t target = (g_Effect.Frame() > 0) ? g_Effect.Frame() - 1 : 0;
        g_Effect.Reset();
        for (uint32_t i = 0; i < target; ++i) g_Effect.Advance(1.0f / 60.0f);
        break;
    }

    case VK_RIGHT:                                   // 1 フレーム進める
        g_Paused = true;
        g_Effect.Advance(1.0f / 60.0f);
        break;

    case 'R': g_Effect.Reset(); break;
    case 'L': g_Effect.SetLoop(!g_Effect.Loop()); break;
    case 'G': g_Layout = !g_Layout; break;
    case 'W': g_NoTexture = !g_NoTexture; break;      // テクスチャ無し（白）で描く
    case 'M':                                          // 合成モードの切替（alpha_add/add/alpha）
    {
        const int next = ((int)GfxGetBlendMode() + 1) % GFX_BLEND_COUNT;
        GfxSetBlendMode((GfxBlendMode)next);
        Log("[blend] %s", GfxBlendModeName(GfxGetBlendMode()));
        break;
    }
    case 'X': g_ShowAxes = !g_ShowAxes; break;        // 座標軸の表示切替（既定 OFF）
    case 'B':                                          // List ブラウザの表示切替
        if (g_hPanel != nullptr)
            ShowWindow(g_hPanel, IsWindowVisible(g_hPanel) ? SW_HIDE : SW_SHOW);
        break;

    case 'T':
        g_Trace = !g_Trace;
        if (g_Trace)
        {
            if (g_TraceFile == nullptr) fopen_s(&g_TraceFile, "effecttest_trace.csv", "wb");
            if (g_TraceFile)
                fprintf(g_TraceFile, "frame,instance,name,elapsed,t01,px,py,pz,sx,sy,sz,r,g,b,a\n");
        }
        else if (g_TraceFile != nullptr)
        {
            fclose(g_TraceFile);
            g_TraceFile = nullptr;
        }
        break;

    case VK_ADD:      g_Effect.SetSpeed(g_Effect.Speed() + 0.1f); break;
    case VK_SUBTRACT:
        g_Effect.SetSpeed((g_Effect.Speed() - 0.1f > 0.1f) ? g_Effect.Speed() - 0.1f : 0.1f);
        break;

    case VK_OEM_4: g_SizeScale = (g_SizeScale > 0.1f) ? g_SizeScale * 0.9f : 0.1f; break;   // '['
    case VK_OEM_6: g_SizeScale *= 1.1f; break;                                             // ']'

    case VK_ESCAPE: g_Running = false; break;

    default:
        if (key >= '1' && key <= '9') g_TexIndex = (int)(key - '1');
        break;
    }
    UpdateTitle();
}

//======================================================================
// List ブラウザ（パネル）
//======================================================================
static std::wstring Widen(const std::string& s)
{
    if (s.empty()) return L"";
    const int need = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (need <= 0) return L"";
    std::wstring w((size_t)need, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], need);
    return w;
}

static void UpdateScheduleLabel(int count)
{
    if (g_hScheduleLabel == nullptr) return;
    wchar_t buf[64];
    if (count < 0) wcscpy_s(buf, L"Schedule  (-)");
    else           swprintf_s(buf, L"Schedule  (%d)", count);
    SetWindowTextW(g_hScheduleLabel, buf);
}

static void ScheduleClear()
{
    if (g_hSchedule != nullptr)
    {
        SendMessageW(g_hSchedule, LB_RESETCONTENT, 0, 0);
        EnableWindow(g_hSchedule, FALSE);
    }
    g_ScheduleNames.clear();
    UpdateScheduleLabel(-1);
}

static void ClearSelection()
{
    g_Effect.ReleaseTextures();
    g_Effect.Reset();
    g_ActionCtx.Reset();
    g_ActionActive = false;
    ScheduleClear();
    g_Dat.Unload();
    g_DatPath.clear();
    g_CommittedItem = -1;
    g_CommittedGroup = -1;
    g_PendingItem = -1;
    g_PendingFrames = 0;
    UpdateTitle();
}

static void LayoutPanel(HWND hPanel)
{
    RECT rc = {};
    GetClientRect(hPanel, &rc);
    const int w = rc.right - 16;
    if (w < 80) return;

    const int bottom = rc.bottom;
    const int schedH = (bottom < 320) ? 96 : 128;
    const int labelH = 18;

    MoveWindow(g_hGroup, 8, 8, w, 200, TRUE);
    MoveWindow(g_hSchedule, 8, bottom - 8 - schedH, w, schedH, TRUE);
    MoveWindow(g_hScheduleLabel, 8, bottom - 8 - schedH - labelH - 4, w, labelH, TRUE);

    int itemsH = bottom - 8 - schedH - labelH - 4 - 8 - 40;
    if (itemsH < 64) itemsH = 64;
    MoveWindow(g_hItems, 8, 40, w, itemsH, TRUE);
}

static void BuildPanelControls(HWND hPanel)
{
    const HINSTANCE hi = (HINSTANCE)GetWindowLongPtrA(hPanel, GWLP_HINSTANCE);

    g_hGroup = CreateWindowExW(0, L"COMBOBOX", L"",
                               WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
                               8, 8, 320, 240, hPanel, (HMENU)IDC_GROUP, hi, nullptr);

    g_hItems = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                               WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
                               8, 40, 320, 320, hPanel, (HMENU)IDC_ITEMS, hi, nullptr);

    g_hScheduleLabel = CreateWindowExW(0, L"STATIC", L"Schedule  (-)",
                                       WS_CHILD | WS_VISIBLE,
                                       8, 400, 320, 18, hPanel, (HMENU)IDC_SCHEDULE_LABEL, hi, nullptr);

    g_hSchedule = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                  WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
                                  8, 420, 320, 128, hPanel, (HMENU)IDC_SCHEDULE, hi, nullptr);
    EnableWindow(g_hSchedule, FALSE);

    FillGroups();
    LayoutPanel(hPanel);
}

//----------------------------------------------------------------------
// グループ（NPC ROM1-9 / Effect カテゴリ）
//----------------------------------------------------------------------
static void FillGroups()
{
    if (g_hGroup == nullptr) return;

    SendMessageW(g_hGroup, CB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < g_List.GroupCount(); ++i)
        SendMessageW(g_hGroup, CB_ADDSTRING, 0, (LPARAM)g_List.Group(i).wlabel.c_str());

    if (g_List.GroupCount() > 0) SendMessageW(g_hGroup, CB_SETCURSEL, 0, 0);
    FillItems();
}

//----------------------------------------------------------------------
// 項目（"名前 (hi/lo)" を列挙）
//----------------------------------------------------------------------
static void FillItems()
{
    if (g_hItems == nullptr) return;

    const int gi = (int)SendMessageW(g_hGroup, CB_GETCURSEL, 0, 0);
    SendMessageW(g_hItems,  LB_RESETCONTENT, 0, 0);
    ScheduleClear();
    g_ActionActive = false;
    g_PendingItem = -1;
    g_PendingFrames = 0;
    g_CommittedItem = -1;
    g_CommittedGroup = -1;

    if (gi < 0) return;

    const std::vector<ListItem>& items = g_List.Items((size_t)gi);
    for (const ListItem& it : items)
    {
        const std::wstring line = it.wname + L"  (" + Widen(it.id) + L")";
        SendMessageW(g_hItems, LB_ADDSTRING, 0, (LPARAM)line.c_str());
    }
    Log("[list] group=%s items=%zu", g_List.Group((size_t)gi).label.c_str(), items.size());
}

//----------------------------------------------------------------------
// 項目の確定（唯一の経路）。Effect / NPC 共通で 0x07 を全件列挙する
//----------------------------------------------------------------------
static void CommitSelection(int itemIndex)
{
    const int gi = (int)SendMessageA(g_hGroup, CB_GETCURSEL, 0, 0);
    if (gi < 0 || g_CommitBusy) return;
    if (gi == g_CommittedGroup && itemIndex == g_CommittedItem) return;

    const ListGroup& g = g_List.Group((size_t)gi);
    const std::vector<ListItem>& items = g_List.Items((size_t)gi);
    if (itemIndex < 0 || itemIndex >= (int)items.size()) return;

    g_CommitBusy = true;

    char path[MAX_PATH] = {};
    if (!ListDb::ResolvePath(g_FfxiBase, g, items[itemIndex], path, MAX_PATH))
    {
        Log("[schedule] clear: resolve failed %s", items[itemIndex].id.c_str());
        ClearSelection();
        g_CommitBusy = false;
        return;
    }

    if (!LoadDatFile(path))
    {
        Log("[schedule] clear: load failed %s", path);
        ClearSelection();
        g_CommitBusy = false;
        return;
    }

    // 0x07 を全件列挙（Effect / NPC 共通）
    ScheduleClear();
    for (const DatChunk& c : g_Dat.Chunks())
        if (c.type == 0x07) g_ScheduleNames.push_back(c.name);

    for (const std::string& n : g_ScheduleNames)
        SendMessageW(g_hSchedule, LB_ADDSTRING, 0, (LPARAM)Widen(n).c_str());

    int def = 0;
    for (size_t i = 0; i < g_ScheduleNames.size(); ++i)
        if (g_ScheduleNames[i] == "main") { def = (int)i; break; }
    if (!g_ScheduleNames.empty()) SendMessageW(g_hSchedule, LB_SETCURSEL, def, 0);
    EnableWindow(g_hSchedule, TRUE);
    UpdateScheduleLabel((int)g_ScheduleNames.size());
    Log("[schedule] count=%zu", g_ScheduleNames.size());

    // Effect: LoadDatFile が既定（main）を構築済み。既定と同一なら再構築しない
    if (g.isEffect && !g_ScheduleNames.empty())
    {
        if (g_ScheduleNames[def] != "main") PlaySelectedSchedule(def);
        g_Paused = false;
    }

    g_CommittedGroup = gi;
    g_CommittedItem  = itemIndex;
    UpdateTitle();
    g_CommitBusy = false;
}

//----------------------------------------------------------------------
// Schedule の選択＝即再生（Effect は g_Effect、NPC は g_ActionCtx）
//----------------------------------------------------------------------
static void PlaySelectedSchedule(int index)
{
    if (index < 0 || index >= (int)g_ScheduleNames.size()) return;
    if (!g_Dat.IsLoaded()) return;

    const std::string& nm = g_ScheduleNames[index];
    const int gi = (int)SendMessageA(g_hGroup, CB_GETCURSEL, 0, 0);
    const bool isEffect = (gi >= 0) && g_List.Group((size_t)gi).isEffect;

    if (isEffect)
    {
        if (!g_Effect.BuildFromChunk(g_Dat, nm.c_str()))
        {
            Log("[schedule] %s : timeline 無し", nm.c_str());
            return;
        }
        g_Effect.BuildTextures(GfxDevice());
        g_Effect.Reset();
        g_ActionActive = false;
        Log("[schedule] effect %s insts=%zu dur=%.3fs", nm.c_str(),
            g_Effect.Timeline().size(), g_Effect.Duration());
        return;
    }

    if (!g_ActionCtx.BuildFromChunk(g_Dat, nm.c_str()))
    {
        Log("[schedule] %s : timeline 無し", nm.c_str());
        return;
    }
    g_ActionCtx.Reset();
    g_ActionActive = true;
    Log("[schedule] npc %s insts=%zu", nm.c_str(), g_ActionCtx.Timeline().size());
}

// 描画ループから毎フレーム呼ぶ。選択が 7 コマ静止したら確定する
static void TickPendingCommit()
{
    if (g_PendingItem < 0) return;
    if (++g_PendingFrames < kCommitDelayFrames) return;

    const int item = g_PendingItem;
    g_PendingItem = -1;
    g_PendingFrames = 0;
    CommitSelection(item);
}

//----------------------------------------------------------------------
// パネルウィンドウプロシージャ
//----------------------------------------------------------------------
static LRESULT CALLBACK PanelProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
        BuildPanelControls(hWnd);
        return 0;

    case WM_SIZE:
        LayoutPanel(hWnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case IDC_GROUP:
            if (HIWORD(wp) == CBN_SELCHANGE) FillItems();
            return 0;

        case IDC_ITEMS:
            if (HIWORD(wp) == LBN_SELCHANGE || HIWORD(wp) == LBN_DBLCLK)
            {
                g_PendingItem = (int)SendMessageA(g_hItems, LB_GETCURSEL, 0, 0);
                g_PendingFrames = 0;
            }
            return 0;

        case IDC_SCHEDULE:
            if (HIWORD(wp) == LBN_SELCHANGE || HIWORD(wp) == LBN_DBLCLK)
                PlaySelectedSchedule((int)SendMessageA(g_hSchedule, LB_GETCURSEL, 0, 0));
            return 0;
        }
        return 0;

    case WM_CLOSE:
        ShowWindow(hWnd, SW_HIDE);          // 閉じてもアプリは終了しない
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

//----------------------------------------------------------------------
// ウィンドウプロシージャ
//----------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_SIZE:
        if (GfxDevice() != nullptr) GfxResize(LOWORD(lp), HIWORD(lp));
        return 0;

    case WM_KEYDOWN: HandleKey(wp, true);  return 0;
    case WM_KEYUP:   HandleKey(wp, false); return 0;

    case WM_LBUTTONDOWN:
        g_Drag = true;
        GetCursorPos(&g_LastMouse);
        SetCapture(hWnd);
        return 0;

    case WM_LBUTTONUP:
        g_Drag = false;
        ReleaseCapture();
        return 0;

    case WM_MOUSEMOVE:
        if (g_Drag)
        {
            POINT pt;
            GetCursorPos(&pt);
            g_Yaw   += (pt.x - g_LastMouse.x) * 0.01f;
            g_Pitch += (pt.y - g_LastMouse.y) * 0.01f;
            if (g_Pitch >  1.5f) g_Pitch =  1.5f;
            if (g_Pitch < -1.5f) g_Pitch = -1.5f;
            g_LastMouse = pt;
        }
        return 0;

    case WM_MOUSEWHEEL:
    {
        const int delta = GET_WHEEL_DELTA_WPARAM(wp);
        g_Dist *= (delta > 0) ? 0.9f : 1.1f;
        if (g_Dist <   0.5f) g_Dist =   0.5f;
        if (g_Dist > 100.0f) g_Dist = 100.0f;
        return 0;
    }

    case WM_DROPFILES:
    {
        char path[MAX_PATH] = {};
        HDROP hd = (HDROP)wp;
        if (DragQueryFileA(hd, 0, path, MAX_PATH) > 0)
        {
            LoadDatFile(path);
            g_Effect.Reset();
            UpdateTitle();
        }
        DragFinish(hd);
        return 0;
    }

    case WM_DESTROY:
        g_Running = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

//----------------------------------------------------------------------
// コマンドライン引数の整形（前後空白の除去 + 引用符の剥がし）
//----------------------------------------------------------------------
static void TrimArg(char* s)
{
    char* b = s;
    while (*b == ' ' || *b == '\t') ++b;
    if (b != s) memmove(s, b, strlen(b) + 1);

    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = '\0';

    if (n >= 2 && s[0] == '"' && s[n - 1] == '"')
    {
        memmove(s, s + 1, n - 2);
        s[n - 2] = '\0';
    }
}

//----------------------------------------------------------------------
// 簡易トークナイザ（引用符対応）
//----------------------------------------------------------------------
static bool NextToken(char*& p, char* out, size_t n)
{
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == '\0') return false;

    const char* s = p;
    if (*p == '"')
    {
        ++p;
        s = p;
        while (*p != '\0' && *p != '"') ++p;
        size_t len = (size_t)(p - s);
        if (len >= n) len = n - 1;
        memcpy(out, s, len);
        out[len] = '\0';
        if (*p == '"') ++p;
    }
    else
    {
        while (*p != '\0' && *p != ' ' && *p != '\t') ++p;
        size_t len = (size_t)(p - s);
        if (len >= n) len = n - 1;
        memcpy(out, s, len);
        out[len] = '\0';
    }
    return true;
}

//----------------------------------------------------------------------
// ヘッドレス自己テスト（GPU 不要 / 結果をテキスト出力）
//   EffectTest.exe --selftest "ROM\11\17.DAT"
//----------------------------------------------------------------------
static int RunSelfTest(const char* path)
{
    FILE* fp = nullptr;
    fopen_s(&fp, "effecttest_selftest.txt", "wb");
    if (fp == nullptr) return 1;

    DatFile dat;
    EffectContext eff;

    if (!dat.Load(path))
    {
        fprintf(fp, "LOAD FAILED: %s\n", path);
        fclose(fp);
        return 2;
    }
    eff.Build(dat);

    fprintf(fp, "file      = %s\n", path);
    fprintf(fp, "size      = %u bytes\n", dat.Size());
    fprintf(fp, "chunks    = %zu\n", dat.Chunks().size());
    fprintf(fp, "generators= %zu\n", eff.Generators().size());
    fprintf(fp, "prog      = 0x%X\n", eff.ProgStart());

    for (const GeneratorBlock& g : eff.Generators())
    {
        fprintf(fp, "  gen %s type=0x%02X size=%u id=%u frames=%u length=%.4fs tblB=0x%X tblC=0x%X frm=%zu%s\n",
                g.chunk.name, g.chunk.type, g.chunk.size, g.id, g.frames, g.Length(), g.tblB, g.tblC,
                g.texFrames.size(), g.HasFrames() ? "" : "(none)");
    }

    fprintf(fp, "timeline  = %zu insts, duration=%.4fs\n", eff.Timeline().size(), eff.Duration());

    unsigned hist[256] = {};
    int spawnCount = 0;
    for (const TimelineInst& in : eff.Timeline())
    {
        hist[in.op]++;
        if (in.op == 0x02) ++spawnCount;
        fprintf(fp, "  +0x%03X op=0x%02X len=%u delay=%u param=%u name=%s\n",
                in.offset, in.op, in.len, in.delay, in.param, in.name);
    }
    fprintf(fp, "spawn(op=0x02)=%d\n", spawnCount);

    fprintf(fp, "op-histogram:");
    for (int i = 0; i < 256; ++i) if (hist[i]) fprintf(fp, " 0x%02X=%u", i, hist[i]);
    fprintf(fp, "\n\n--- simulate 240 frames @ 60fps ---\n");

    for (int f = 0; f < 240; ++f)
    {
        eff.Advance(1.0f / 60.0f);

        if (f < 12 || f == 59 || f == 119 || f == 239)
        {
            fprintf(fp, "frame=%3d ip=%zu/%zu inst=%zu",
                    f, eff.Ip(), eff.Timeline().size(), eff.Instances().size());
            for (const MotionInstance& mi : eff.Instances())
            {
                const float* p = mi.sub.At(SUB_POS);
                const float* s = mi.sub.At(SUB_SCALE);
                const float* c = mi.sub.At(SUB_COLOR);
                fprintf(fp,
                        " | %s t=%.3f/%.3f pos=(%.3f,%.3f,%.3f) scl=(%.3f,%.3f,%.3f) col=(%.2f,%.2f,%.2f,%.2f) tex=%d link=%s uv=(%.3f,%.3f,%.3f,%.3f)",
                        mi.gen->chunk.name, mi.sub.Elapsed(), mi.gen->Length(),
                        p[0], p[1], p[2], s[0], s[1], s[2], c[0], c[1], c[2], c[3],
                        mi.texIndex, mi.gen->linkName,
                        mi.uv[0], mi.uv[1], mi.uv[2], mi.uv[3]);
            }
            fprintf(fp, "\n");
        }
    }

    fprintf(fp, "\nsubtl    = %zu\n", eff.SubTimelineCount());

    fclose(fp);
    return 0;
}

//----------------------------------------------------------------------
// エントリポイント
//----------------------------------------------------------------------
int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE, LPSTR lpCmdLine, int)
{
    // VM の乱数（op 0x0A/0x10/0x11）を固定し、--shot / --selftest の
    // 画素数・位置を実行ごとに再現できるようにする（検証のゴールデン用）
    srand(1);

    // 自己テストモード（GPU / ウィンドウ不要）
    if (lpCmdLine != nullptr && strncmp(lpCmdLine, "--selftest", 10) == 0)
    {
        const char* p = lpCmdLine + 10;
        char path[MAX_PATH] = {};
        strncpy_s(path, p, _TRUNCATE);
        TrimArg(path);
        return RunSelfTest(path);
    }

    //--------------------------------------------------------------
    // List 内容のダンプ（GPU 不要）: --list
    //--------------------------------------------------------------
    if (lpCmdLine != nullptr && strncmp(lpCmdLine, "--list", 6) == 0)
    {
        if (!FindFFXI(g_FfxiBase, sizeof(g_FfxiBase))) { Log("[list] FFXI 未検出"); return 2; }

        char listDir[MAX_PATH] = {};
        if (!FindListDir(listDir, MAX_PATH)) { Log("[list] List フォルダが見つかりません"); return 3; }
        if (!g_List.Load(listDir)) { Log("[list] Load 失敗: %s", listDir); return 3; }

        Log("[list] groups=%zu  ffxi=%s", g_List.GroupCount(), g_FfxiBase);
        for (size_t gi = 0; gi < g_List.GroupCount(); ++gi)
        {
            const ListGroup& g = g_List.Group(gi);
            const std::vector<ListItem>& items = g_List.Items(gi);
            Log("[list] %2zu %-40s file=%-22s rom=%d items=%zu",
                gi, g.label.c_str(), g.file.c_str(), g.rom, items.size());

            for (int k = 0; k < 3 && k < (int)items.size(); ++k)
            {
                char path[MAX_PATH] = {};
                ListDb::ResolvePath(g_FfxiBase, g, items[k], path, MAX_PATH);
                Log("[list]      %-30s id=%-10s -> %s",
                    items[k].name.c_str(), items[k].id.c_str(), path);
            }
        }
        return 0;
    }

    //--------------------------------------------------------------
    // アクション timeline のダンプ: --action "<dat>" [name]
    //   name 省略時はその DAT の 0x07 名を列挙
    //--------------------------------------------------------------
    if (lpCmdLine != nullptr && strncmp(lpCmdLine, "--action", 8) == 0)
    {
        char* p = lpCmdLine + 8;
        char dat[MAX_PATH] = {};
        char nm[32] = {};
        if (!NextToken(p, dat, MAX_PATH)) return 2;
        NextToken(p, nm, sizeof(nm));

        DatFile d;
        if (!d.Load(dat)) { Log("[action] load failed: %s", dat); return 3; }

        if (nm[0] == '\0')
        {
            Log("[action] %s : 0x07 ブロック一覧", dat);
            for (const DatChunk& c : d.Chunks())
                if (c.type == 0x07)
                    Log("[action]   %s size=%u", c.name, c.size);
            return 0;
        }

        EffectContext ctx;
        if (!ctx.BuildFromChunk(d, nm)) { Log("[action] %s : timeline 無し", nm); return 4; }

        Log("[action] %s insts=%zu dur=%.3fs", nm, ctx.Timeline().size(), ctx.Duration());
        for (const TimelineInst& in : ctx.Timeline())
            Log("[action]   +0x%03X op=0x%02X len=%u delay=%u param=%u name=%s",
                in.offset, in.op, in.len, in.delay, in.param, in.name);
        return 0;
    }

    //--------------------------------------------------------------
    // ベースエフェクト（ROM\0\0.DAT）の索引ダンプ: --base
    //--------------------------------------------------------------
    if (lpCmdLine != nullptr && strncmp(lpCmdLine, "--base", 6) == 0)
    {
        if (!FindFFXI(g_FfxiBase, sizeof(g_FfxiBase))) { Log("[base] FFXI 未検出"); return 2; }

        char basePath[MAX_PATH] = {};
        sprintf_s(basePath, "%s\\ROM\\0\\0.DAT", g_FfxiBase);

        const ULONGLONG t0 = 0; (void)t0;
        if (!g_Base.Load(nullptr, basePath)) { Log("[base] 読み込み失敗: %s", basePath); return 3; }

        // テクスチャは D3D デバイス無しでは SRV 化しないため、チャンク数で報告する
        size_t texChunks = 0;
        for (const DatChunk& c : g_Base.Dat().Chunks())
            if (c.type == 0x20) ++texChunks;

        Log("[base] path=%s chunks=%zu gen=%zu tex=%zu timeline=%zu load_ms=%d",
            basePath, g_Base.ChunkCount(), g_Base.GeneratorCount(),
            texChunks, g_Base.TimelineCount(), g_Base.LoadMs());
        return 0;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = kWndClass;
    if (RegisterClassExW(&wc) == 0) return 1;

    RECT rc = { 0, 0, kInitW, kInitH };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    g_hWnd = CreateWindowExW(WS_EX_ACCEPTFILES, kWndClass, L"EffectTest",
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             rc.right - rc.left, rc.bottom - rc.top,
                             nullptr, nullptr, hInst, nullptr);
    if (g_hWnd == nullptr) return 1;

    DragAcceptFiles(g_hWnd, TRUE);          // ★ドラッグ＆ドロップを有効化

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if (!GfxInit(g_hWnd, kInitW, kInitH))
    {
        MessageBoxW(g_hWnd, L"DirectX11 の初期化に失敗しました。", L"EffectTest", MB_ICONERROR);
        return 1;
    }
    ShowWindow(g_hWnd, SW_SHOW);

    //--------------------------------------------------------------
    // スクリーンショットモード（検証用）: --shot <dat> [out.png]
    //--------------------------------------------------------------
    if (lpCmdLine != nullptr && strncmp(lpCmdLine, "--shot", 6) == 0)
    {
        char* p = lpCmdLine + 6;
        char dat[MAX_PATH] = {};
        char png[MAX_PATH] = "effecttest_shot.png";

        // --blend alpha_add|add|alpha（検証用。省略時は alpha_add）
        for (;;)
        {
            char tok[MAX_PATH] = {};
            if (!NextToken(p, tok, MAX_PATH)) break;

            if (_stricmp(tok, "--blend") == 0)
            {
                char mode[MAX_PATH] = {};
                if (!NextToken(p, mode, MAX_PATH)) return 2;
                if      (_stricmp(mode, "alpha_add") == 0) GfxSetBlendMode(GFX_BLEND_ALPHA_ADD);
                else if (_stricmp(mode, "add")       == 0) GfxSetBlendMode(GFX_BLEND_ADD);
                else if (_stricmp(mode, "alpha")     == 0) GfxSetBlendMode(GFX_BLEND_ALPHA);
                else { Log("[shot] unknown blend: %s", mode); return 2; }
                continue;
            }

            if (_stricmp(tok, "--uv") == 0)
            {
                char mode[MAX_PATH] = {};
                if (!NextToken(p, mode, MAX_PATH)) return 2;
                if      (_stricmp(mode, "0x21") == 0) g_Effect.SetUseTexFrames(true);
                else if (_stricmp(mode, "full") == 0) g_Effect.SetUseTexFrames(false);
                else { Log("[shot] unknown uv: %s", mode); return 2; }
                continue;
            }

            if (dat[0] == 0) strncpy_s(dat, tok, _TRUNCATE);
            else             strncpy_s(png, tok, _TRUNCATE);
        }
        if (dat[0] == 0) return 2;

        // base（ROM\0\0.DAT）を先に読み、+0xAC が base の 0x20 を指す場合に借用できるようにする
        if (FindFFXI(g_FfxiBase, sizeof(g_FfxiBase)))
        {
            char basePath[MAX_PATH] = {};
            sprintf_s(basePath, "%s\\ROM\\0\\0.DAT", g_FfxiBase);
            if (g_Base.Load(GfxDevice(), basePath))
            {
                g_Effect.SetBaseTexture(&g_Base);
                Log("[base] path=%s chunks=%zu gen=%zu tex=%zu timeline=%zu load_ms=%d",
                    basePath, g_Base.ChunkCount(), g_Base.GeneratorCount(),
                    g_Base.TextureCount(), g_Base.TimelineCount(), g_Base.LoadMs());
            }
            else Log("[base] 読み込み失敗（継続）: %s", basePath);
        }

        if (!LoadDatFile(dat)) { Log("[shot] load failed: %s", dat); return 3; }

        for (int i = 0; i < 90; ++i) g_Effect.Advance(1.0f / 60.0f);

        // 描画入力のダンプ（原因切り分け用）
        for (const TimTexture& t : g_Effect.Textures())
            Log("[shot] tex %s %ux%u srv=%p", t.name.c_str(), t.width, t.height, (void*)t.srv);

        for (const MotionInstance& mi : g_Effect.Instances())
        {
            const float* pp = mi.sub.At(SUB_POS);
            const float* ss = mi.sub.At(SUB_SCALE);
            const float* cc = mi.sub.At(SUB_COLOR);
            Log("[shot] inst %s pos=(%.2f,%.2f,%.2f) scl=(%.2f,%.2f,%.2f) col=(%.2f,%.2f,%.2f,%.2f) tex=%d link=%s uv=(%.3f,%.3f,%.3f,%.3f) frm=%d/%zu srv=%p",
                mi.gen->chunk.name, pp[0], pp[1], pp[2], ss[0], ss[1], ss[2],
                cc[0], cc[1], cc[2], cc[3], mi.texIndex, mi.gen->linkName,
                mi.uv[0], mi.uv[1], mi.uv[2], mi.uv[3], mi.frameIdx, mi.gen->texFrames.size(), (void*)mi.srv);
        }

        RenderScene(false);                     // Present せずに描画

        wchar_t wpng[MAX_PATH] = {};
        MultiByteToWideChar(CP_ACP, 0, png, -1, wpng, MAX_PATH);
        const bool ok = GfxSaveBackBufferPNG(wpng);

        Log("[shot] dat=%s png=%s blend=%s uvmode=%s inst=%zu tex=%zu ok=%d",
            dat, png, GfxBlendModeName(GfxGetBlendMode()),
            g_Effect.UseTexFrames() ? "0x21" : "full",
            g_Effect.Instances().size(), g_Effect.Textures().size(), ok ? 1 : 0);

        g_Effect.ReleaseTextures();
        g_Dat.Unload();
        g_Base.Unload();
        GfxShutdown();
        CoUninitialize();
        return ok ? 0 : 4;
    }

    //--------------------------------------------------------------
    // FFXI 検出 + ベースエフェクト（ROM\0\0.DAT = syst）の起動時ロード
    //--------------------------------------------------------------
    if (!FindFFXI(g_FfxiBase, sizeof(g_FfxiBase)))
    {
        Log("[base] FFXI 未検出: ベースは読み込めません");
    }
    else
    {
        char basePath[MAX_PATH] = {};
        sprintf_s(basePath, "%s\\ROM\\0\\0.DAT", g_FfxiBase);

        if (g_Base.Load(GfxDevice(), basePath))
        {
            g_Effect.SetBaseTexture(&g_Base);      // +0xAC が base の 0x20 を指す場合の借用元
            Log("[base] path=%s chunks=%zu gen=%zu tex=%zu timeline=%zu load_ms=%d",
                basePath, g_Base.ChunkCount(), g_Base.GeneratorCount(),
                g_Base.TextureCount(), g_Base.TimelineCount(), g_Base.LoadMs());
        }
        else
            Log("[base] 読み込み失敗（継続）: %s", basePath);
    }

    //--------------------------------------------------------------
    // List ブラウザ（パネルウィンドウ）
    //--------------------------------------------------------------
    if (g_FfxiBase[0] != '\0')
    {
        char listDir[MAX_PATH] = {};
        if (FindListDir(listDir, MAX_PATH) && g_List.Load(listDir))
            Log("[list] groups=%zu  (%s)", g_List.GroupCount(), listDir);
        else
            Log("[list] List が見つかりません");

        WNDCLASSEXW pc = {};
        pc.cbSize        = sizeof(pc);
        pc.lpfnWndProc   = PanelProc;
        pc.hInstance     = hInst;
        pc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        pc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        pc.lpszClassName = kPanelClass;
        RegisterClassExW(&pc);

        RECT mr = {};
        GetWindowRect(g_hWnd, &mr);
        g_hPanel = CreateWindowExW(WS_EX_TOOLWINDOW, kPanelClass, L"List  (グループ / 項目 / Schedule)",
                                   WS_OVERLAPPEDWINDOW,
                                   mr.right + 8, mr.top, 400, 640,
                                   nullptr, nullptr, hInst, nullptr);
        if (g_hPanel != nullptr) ShowWindow(g_hPanel, SW_SHOW);
    }
    else
    {
        Log("[list] FFXI 未検出のため List ブラウザは無効です");
    }

    //--------------------------------------------------------------
    // 起動引数の DAT / 無ければ既定エフェクトを自動読込
    //--------------------------------------------------------------
    if (lpCmdLine != nullptr && lpCmdLine[0] != '\0')
    {
        char path[MAX_PATH] = {};
        strncpy_s(path, lpCmdLine, _TRUNCATE);
        TrimArg(path);

        if (strcmp(path, "--auto") == 0)
            LoadDefaultEffect();                    // 明示指定時のみ既定エフェクトを読む
        else if (!LoadDatFile(path))
            Log("[arg] load failed: %s", path);
    }
    // 引数なし: 起動時は何も読み込まない／表示しない（List の項目選択がトリガー）
    UpdateTitle();

    //--------------------------------------------------------------
    // メインループ
    //--------------------------------------------------------------
    LARGE_INTEGER freq = {}, prev = {}, now = {};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    MSG msg = {};
    while (g_Running)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) g_Running = false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!g_Running) break;

        QueryPerformanceCounter(&now);
        g_Dt = (float)((double)(now.QuadPart - prev.QuadPart) / (double)freq.QuadPart);
        prev = now;
        if (g_Dt > 0.1f) g_Dt = 0.1f;                    // タブ切れ等の暴走防止

        if (!g_Paused && g_Dat.IsLoaded()) g_Effect.Advance(g_Dt);
        if (!g_Paused && g_ActionActive)   g_ActionCtx.Advance(g_Dt);

        TickPendingCommit();

        RenderScene();
        UpdateTitle();
    }

    if (g_TraceFile != nullptr) { fclose(g_TraceFile); g_TraceFile = nullptr; }
    g_Effect.ReleaseTextures();
    g_Base.Unload();
    g_Dat.Unload();
    GfxShutdown();
    CoUninitialize();
    return 0;
}



