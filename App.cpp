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
#include <algorithm>

#include "Dat.h"
#include "Effect.h"
#include "EffectBase.h"
#include "Skeleton.h"
#include "Gfx.h"
#include "ListDb.h"

using namespace DirectX;

//----------------------------------------------------------------------
// グローバル状態
//----------------------------------------------------------------------
namespace
{
    const wchar_t* kWndClass = L"EffectTestWindow";
    int            g_InitW   = 1280;   // 撮影解像度（--res WxH。参照の 3D ビューは 625x512）
    int            g_InitH   = 720;

    HWND  g_hWnd    = nullptr;
    bool  g_Running = true;

    DatFile       g_Dat;
    EffectContext g_Effect;
    std::string   g_DatPath;

    bool  g_Paused    = false;
    bool  g_Layout    = false;    // 位置 0 のインスタンスを横並びにする（確認用・既定 off）
                                  //   ※ 既定 on だと、まだ位置を計算できていないインスタンスが
                                  //     横 1.6 m 間隔で並び「横長の帯」になる（実測: 11\18 の
                                  //     fire が aspect 2.3 の横帯。参照は 0.40 の縦柱）
    bool  g_ZUp       = false;    // 粒子位置を Z-up → Y-up 変換する（--zup on）
    bool  g_OpStat    = false;    // 実行された VM op の集計を出力する（--opstat）
    bool  g_Trace     = false;
    bool  g_NoTexture = false;    // W: テクスチャを使わず白で描く（デバッグ）
    bool  g_ShowAxes  = false;    // X: 座標軸の表示（既定 OFF。起動時は何も描かない）
    float g_SizeScale = 1.0f;     // ビルボードの基準半径
    // UV オフセット（sub+0xB4/0xB8）。0=off / 1=0..1 巡回 / 2=セル内巡回
    //   問題 2: bool だと `cell`(=2) が true(=1) に化けて 2 が到達不能になる。
    int   g_UvScroll  = 0;
    bool  g_BlendAuto = false;    // --blend auto: テクスチャの黒割合で alpha / add を自動選択
    bool  g_GeoFlat   = false;    // 0x1F の XZ 平面を地面デカールとして描く（--geoflat on）
    int   g_SizeMode  = 3;        // 0: scale / 1: 0x21 quad / 2: 正方形（scale[2]）/ 3: メートル（既定）
    float g_Gain      = 1.5f;     // 色のゲイン（PS の ×1.5 と等価。--gain で A/B）
    // RGB=0 → 白 / α≤0 → 1 の代替（問題 7）。既定 on（現状維持）、--colorfallback off で A/B
    bool  g_ColorFallback = true;

// 骨格をエフェクトのルート変換として使う（--model / --motion / --bone）
bool      g_RootActive = false;
float     g_Root[16]   = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
int       g_RootBone   = 0;

// インスタンス毎の骨（--bone-map "0:10,1:20"）。骨は 0 始まりの配列番号。
std::vector<float> g_InstMats;      // [instance*16]
std::vector<char>  g_InstHasBone;   // 1 = 骨あり
float g_ClearColor[4] = { 0.05f, 0.06f, 0.09f, 1.0f };   // --bg で変更（参照動画と合わせる用）
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
    // 問題 10: vsprintf_s はバッファ不足で無効パラメータハンドラ → 異常終了する。
    //   切り詰めて必ず成功させる（書式の誤りだけは _set_invalid_parameter_handler 側のまま）。
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
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

// Effect.h から呼ばれるログ窓口（Effect.cpp は Log() を持たない）
//   Log() が改行を付けるため、呼び出し側の書式に "\n" は書かない。
void EffectLogLine(const char* fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);

    // 改行は呼び出し側の書式に含まれるため、そのまま渡す
    Log("%s", buf);
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
// モデル DAT の解決
//   "human" / "0-27-82" → <FFXI>\ROM\27\82.DAT（PC ヒュームの基準モデル）
//   List の ID 規則（ListDb::ResolvePath）と同じ: rom<=1 は ROM\<hi>\<lo>.DAT
//----------------------------------------------------------------------
static bool ResolveModelPath(const char* arg, char* out, size_t outSize)
{
    if (arg == nullptr || arg[0] == '\0') return false;

    if (_stricmp(arg, "human") == 0 || strcmp(arg, "0-27-82") == 0)
    {
        char base[MAX_PATH] = {};
        if (!FindFFXI(base, sizeof(base))) return false;
        sprintf_s(out, outSize, "%s\\ROM\\27\\82.DAT", base);
        return true;
    }

    strncpy_s(out, outSize, arg, _TRUNCATE);
    return true;
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

    const float* clear = g_ClearColor;
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

        // 骨格のルート変換（--model / --motion / --bone / --bone-map）はインスタンス単位
        const float* Mptr = nullptr;
        if (i < (int)g_InstHasBone.size() && g_InstHasBone[i])
            Mptr = &g_InstMats[(size_t)i * 16];
        else if (g_RootActive)
            Mptr = g_Root;

        const int texIdx = g_NoTexture ? -1
                                       : ((mi.texIndex >= 0) ? mi.texIndex : g_TexIndex);

        // base の 0x20 を借用している場合はそちらを優先（未解決なら従来どおり白）
        ID3D11ShaderResourceView* srv = g_NoTexture ? nullptr : mi.srv;
        if (!g_NoTexture && srv == nullptr) srv = g_Effect.TextureSrv(texIdx);

        // テクスチャが解決できないインスタンスは描かない
        //   （srv=null で描くと黒クワッドになり、横帯などのアーティファクトになる）
        if (!g_NoTexture && srv == nullptr) continue;

        // 粒子（サブ）ごとに 1 枚描く（AltanaView の DrawPrimitive(…, subs*2) 相当）
        const GfxBlendMode savedBlend = GfxGetBlendMode();
        if (g_BlendAuto)
            GfxSetBlendMode((mi.blackRatio > 0.5f) ? GFX_BLEND_ADD : GFX_BLEND_ALPHA);

        for (int k = 0; k < mi.SubCount(); ++k)
        {
            const SubUnit& su = mi.subs[(size_t)k];
            const float* p = su.sub.At(SUB_POS);
            const float* s = su.sub.At(SUB_SCALE);
            const float* c = su.sub.At(SUB_COLOR);

            float center[3] = { p[0], p[1], p[2] };

            // データ2 op 0x01 の位置オフセット（参考: cosmosa の「float x, y, z」）
            if (mi.gen->hasPartOfs)
            {
                center[0] += mi.gen->partOfs[0];
                center[1] += mi.gen->partOfs[1];
                center[2] += mi.gen->partOfs[2];
            }

            // 位置が未設定のエフェクトでも形を確認できるよう横並びに配置
            if (g_Layout && center[0] == 0.0f && center[1] == 0.0f && center[2] == 0.0f)
                center[0] = (i - (count - 1) * 0.5f) * 1.6f;

            // Z-up → Y-up 変換（--zup on）
            //   データの粒子は Z 方向へ動く（実測: g000 が f30→f120 で
            //   pos=(0.08,-0.12,-2.00) → (0.44,-0.66,-11.00)）。
            //   参照動画の炎は Y（上）方向へ伸びるため、軸の入れ替えを試せるようにする。
            if (g_ZUp)
            {
                const float ty = center[1];
                center[1] = center[2];
                center[2] = -ty;
            }

            const float sx  = (s[0] > 0.001f) ? s[0] : 1.0f;
            const float sy2 = (s[1] > 0.001f) ? s[1] : 1.0f;

            // 0x1F が地面平面（Y=0 の XZ クワッド）なら、カメラ正対ではなく世界軸で描く
            //   ※ 既定は無効（--geoflat on で有効）
            //   データの bil=0（平面）なら 0x1F が無くても平面として描く
            const bool  flat = g_GeoFlat &&
                               ((mi.gen->bil == 0) ||
                                (mi.gen->hasGeo && mi.gen->geoFlat));
            float       wR[3] = { 1.0f, 0.0f, 0.0f };
            float       wU[3] = { 0.0f, 0.0f, 1.0f };
            const float* axR = flat ? wR : right;
            const float* axU = flat ? wU : up;

            // 0x21 クワッドのローカル範囲（±2 など）を寸法に使う場合（--size quad）
            float halfW = g_SizeScale * sx;
            float halfH = g_SizeScale * sy2;
            if (g_SizeMode == 1 && su.quadHalf[0] > 0.0f && su.quadHalf[1] > 0.0f)
            {
                // 0x21 のクワッド範囲は既にメートル実寸（例: ±2 → 4 m 角）。
                // VM のスケール（scl）は現状 X 成分が破綻しているため掛けない。
                // 倍率を変えたいときは --scale を使う。
                halfW = g_SizeScale * su.quadHalf[0];
                halfH = g_SizeScale * su.quadHalf[1];

                // クワッド中心のオフセット（ローカル xy）
                center[0] += axR[0] * su.quadCenter[0] + axU[0] * su.quadCenter[1];
                center[1] += axR[1] * su.quadCenter[0] + axU[1] * su.quadCenter[1];
                center[2] += axR[2] * su.quadCenter[0] + axU[2] * su.quadCenter[1];
            }
            else if (g_SizeMode == 4)
            {
                // クワッド実寸 × VM の Scale（--size quadscale）
                //   0x21 のローカル範囲は「テクスチャ 1 枚ぶんの実寸」で、倍率は
                //   VM の Scale が担う。Cure 10/9 の prn は ±2 m × 0.1 = 0.2 m 角
                //   となり、参照の星の大きさ（約 20〜40 px @ dist 2.785）と一致する。
                if (su.quadHalf[0] > 0.0f && su.quadHalf[1] > 0.0f)
                {
                    halfW = g_SizeScale * su.quadHalf[0] * sx;
                    halfH = g_SizeScale * su.quadHalf[1] * sy2;
                    center[0] += axR[0] * su.quadCenter[0] * sx + axU[0] * su.quadCenter[1] * sy2;
                    center[1] += axR[1] * su.quadCenter[0] * sx + axU[1] * su.quadCenter[1] * sy2;
                    center[2] += axR[2] * su.quadCenter[0] * sx + axU[2] * su.quadCenter[1] * sy2;
                }
                else
                {
                    halfW = g_SizeScale * sx;
                    halfH = g_SizeScale * sy2;
                }
            }
            else if (g_SizeMode == 2)
            {
                // 正方形ビルボード（ワイヤーフレーム参照に合わせる）: scale[2] を両軸に使う
                const float sz = (s[2] > 0.001f) ? s[2] : 1.0f;
                halfW = g_SizeScale * sz;
                halfH = g_SizeScale * sz;
            }
            else if (g_SizeMode == 3)
            {
                // メートル基準: 0x21 のクワッド範囲（±2 m など）、無ければ 1.0 m
                if (su.quadHalf[0] > 0.0f && su.quadHalf[1] > 0.0f)
                {
                    halfW = g_SizeScale * su.quadHalf[0];
                    halfH = g_SizeScale * su.quadHalf[1];
                    center[0] += axR[0] * su.quadCenter[0] + axU[0] * su.quadCenter[1];
                    center[1] += axR[1] * su.quadCenter[0] + axU[1] * su.quadCenter[1];
                    center[2] += axR[2] * su.quadCenter[0] + axU[2] * su.quadCenter[1];
                }
                else
                {
                    halfW = g_SizeScale * 1.0f;
                    halfH = g_SizeScale * 1.0f;
                }
            }

            float axisR[3] = { axR[0] * halfW, axR[1] * halfW, axR[2] * halfW };
            float axisU[3] = { axU[0] * halfH,    axU[1] * halfH,    axU[2] * halfH };

            // 面内回転（--spin on）: スピン角（SubObject 0xBC の Z 成分）で
            //   クワッドを視線まわりに回す（ビルボードのロール）。
            //   VM#1 0x3D/0x3E/0x40 と VM#2 0x29/0x2B が角度を積む。
            if (EffectSpin())
            {
                const float* sp = su.sub.At(SUB_SPIN);
                const float roll = sp[2];
                if (roll != 0.0f)
                {
                    const float cr = cosf(roll), sr = sinf(roll);
                    for (int a = 0; a < 3; ++a)
                    {
                        const float r0 = axisR[a], u0 = axisU[a];
                        axisR[a] =  r0 * cr + u0 * sr;
                        axisU[a] = -r0 * sr + u0 * cr;
                    }
                }
            }

            // 0x1F のサブクワッド描画（--geo on）: 6 頂点ごとに 1 枚ずつ描く
            if (g_Effect.Geo() && mi.gen->hasGeo && !mi.gen->geoQuads.empty())
            {
                float rgbaG[4] = { c[0], c[1], c[2], c[3] };
                if (g_ColorFallback && rgbaG[0] == 0.0f && rgbaG[1] == 0.0f && rgbaG[2] == 0.0f)
                    rgbaG[0] = rgbaG[1] = rgbaG[2] = 1.0f;
                bool drewGeo = false;
                // α が 0 のときは粒子がフェード済み＝ジオメトリも描かない
                //   （以前は α を 1 に強制していたため、消えた粒子の板が残っていた）
                if (rgbaG[3] > 0.0f)
                {
                    for (const GeneratorBlock::GeoQuad& gq : mi.gen->geoQuads)
                    {
                        // 3D の実頂点形状（A/B/C から作った平行四辺形）をそのまま使う
                        //   0x1F の頂点は実寸（m）なので、インスタンスの Scale は掛けない
                        //   （Scale はビルボード／0x21 のサイズ側で効く）
                        float cq[3] = { center[0] + gq.ctr[0],
                                        center[1] + gq.ctr[1],
                                        center[2] + gq.ctr[2] };
                        float aR[3] = { gq.aR[0] * g_SizeScale,
                                        gq.aR[1] * g_SizeScale,
                                        gq.aR[2] * g_SizeScale };
                        float aU[3] = { gq.aU[0] * g_SizeScale,
                                        gq.aU[1] * g_SizeScale,
                                        gq.aU[2] * g_SizeScale };

                        if (Mptr != nullptr)
                        {
                            // 位置は w=1、辺ベクトルは w=0 で変換（アフィン）
                            const XMMATRIX M = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(Mptr));
                            XMFLOAT3 tmp = {};
                            XMStoreFloat3(&tmp, XMVector3Transform(XMVectorSet(cq[0], cq[1], cq[2], 1.0f), M));
                            cq[0] = tmp.x; cq[1] = tmp.y; cq[2] = tmp.z;
                            XMStoreFloat3(&tmp, XMVector3TransformNormal(XMVectorSet(aR[0], aR[1], aR[2], 0.0f), M));
                            aR[0] = tmp.x; aR[1] = tmp.y; aR[2] = tmp.z;
                            XMStoreFloat3(&tmp, XMVector3TransformNormal(XMVectorSet(aU[0], aU[1], aU[2], 0.0f), M));
                            aU[0] = tmp.x; aU[1] = tmp.y; aU[2] = tmp.z;
                        }

                        GfxDrawQuad(cq, aR, aU, rgbaG, gq.uv, srv);
                        drewGeo = true;
                    }
                }

                if (g_TraceFile != nullptr)
                {
                    fprintf(g_TraceFile,
                            "%u,%d,%d,%s,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.4f,%.4f,%.4f,%.4f\n",
                            g_Effect.Frame(), i, k, mi.gen->chunk.name,
                            su.sub.Elapsed(), su.sub.T01(),
                            p[0], p[1], p[2], s[0], s[1], s[2],
                            c[0], c[1], c[2], c[3]);
                }
                // 0x1F がある generator は形状で描く（ビルボードはスキップ）。
                //   α=0 の粒子はジオメトリも描かないため、その時間帯は何も出ない
                //   （例: `11\27` は全粒子 α=0 → `--geo on` では空白。既定描画は
                //     後述の α 代替（α=1 強制）で見えているだけ）。
                continue;
            }

            if (Mptr != nullptr)
            {
                const XMMATRIX M = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(Mptr));
                XMFLOAT3 tmp = {};
                XMStoreFloat3(&tmp, XMVector3Transform(XMVectorSet(center[0], center[1], center[2], 1.0f), M));
                center[0] = tmp.x; center[1] = tmp.y; center[2] = tmp.z;

                // ビルボードの軸は回転させない（常にカメラ正対）。
                // 骨行列は「中心位置」にだけ適用する。軸まで回すと、軸が視線方向に
                // 倒れたときにクワッドが潰れて帯状になる（実測で発生）。
            }

            float rgba[4] = { c[0], c[1], c[2], c[3] };
            // 問題 7: 代替処理は不具合（キー表の未解決など）を隠すため、
            //   --colorfallback off で無効化して A/B できるようにする（既定 on）。
            if (g_ColorFallback)
            {
                if (rgba[0] == 0.0f && rgba[1] == 0.0f && rgba[2] == 0.0f)
                {
                    rgba[0] = rgba[1] = rgba[2] = 1.0f;  // VM が色を書いていない場合の代替
                }
                if (rgba[3] <= 0.0f) rgba[3] = 1.0f;
            }

            // 注: 0x21 の頂点色（+12 の D3DCOLOR）の乗算は、当方の解釈では黒化して
            //     破綻したため未適用。AltanaView は FUN_00401f70 で (v * inst) >> 7 を
            //     行うが、頂点レコード内の色位置は再検証が必要（docs/ANALYSIS-AltanaView-Draw.md §5.2）。

            // UV オフセット（sub+0xB4 = u, sub+0xB8 = v）。累積値は 0..1 を巡回させる
            //   ※ 既定は無効（--uvscroll on で有効）。アトラス（4x4 等）では巡回先が
            //     別セルになるため、見た目はむしろ崩れる（実測）。
            const float* uvo = su.sub.At(0xB4);
            float uvDraw[4] = { su.uv[0], su.uv[1], su.uv[2], su.uv[3] };
            if (g_UvScroll)
            {
                const float uw = su.uv[2] - su.uv[0];
                const float vh = su.uv[3] - su.uv[1];
                float du = uvo[0];
                float dv = uvo[1];
                if (g_UvScroll == 1)          // 0..1 を巡回
                {
                    du -= floorf(du);
                    dv -= floorf(dv);
                }
                else                          // セル内を巡回（アトラスの 1 セル分）
                {
                    if (uw > 1e-6f) du -= floorf(du / uw) * uw;
                    if (vh > 1e-6f) dv -= floorf(dv / vh) * vh;
                }
                float u0n = su.uv[0] + du;
                float v0n = su.uv[1] + dv;
                if (u0n + uw > 1.0f) u0n -= 1.0f;
                if (v0n + vh > 1.0f) v0n -= 1.0f;
                uvDraw[0] = u0n; uvDraw[1] = v0n;
                uvDraw[2] = u0n + uw; uvDraw[3] = v0n + vh;
            }

            // --gain: PS の ×1.5 を相殺して任意のゲインにする（A/B 用）
            if (g_Gain != 1.5f)
            {
                const float k = g_Gain / 1.5f;
                rgba[0] *= k; rgba[1] *= k; rgba[2] *= k;
            }

            GfxDrawQuad(center, axisR, axisU, rgba, uvDraw, srv);

            if (g_TraceFile != nullptr)
            {
                fprintf(g_TraceFile,
                        "%u,%d,%d,%s,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.4f,%.4f,%.4f,%.4f\n",
                        g_Effect.Frame(), i, k, mi.gen->chunk.name,
                        su.sub.Elapsed(), su.sub.T01(),
                        p[0], p[1], p[2], s[0], s[1], s[2],
                        c[0], c[1], c[2], c[3]);
            }
        }

        if (g_BlendAuto && savedBlend != GfxGetBlendMode())
            GfxSetBlendMode(savedBlend);
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
                fprintf(g_TraceFile, "frame,instance,sub,name,elapsed,t01,px,py,pz,sx,sy,sz,r,g,b,a\n");
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

    // キー表参照（0x19 名）の解決監査（問題 1）。「解決率 100%」がゴールデン
    const KeyAudit ka = EffectKeyAudit(dat, eff.Generators());

    fprintf(fp, "file      = %s\n", path);
    fprintf(fp, "size      = %u bytes\n", dat.Size());
    fprintf(fp, "chunks    = %zu\n", dat.Chunks().size());
    fprintf(fp, "generators= %zu\n", eff.Generators().size());
    fprintf(fp, "prog      = 0x%X\n", eff.ProgStart());
    fprintf(fp, "keys      = %d ops, %d resolved, %d unresolved\n",
            ka.ops, ka.resolved, ka.unresolved);
    // 0x20 の数と specific 名の数は一致する（問題 3 の観測点）
    fprintf(fp, "texnames  = %zu texspecific = %zu\n",
            eff.TexNameCount(), eff.TexSpecificCount());

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
                const SubUnit& su = mi.subs[0];
                const float* p = su.sub.At(SUB_POS);
                const float* s = su.sub.At(SUB_SCALE);
                const float* c = su.sub.At(SUB_COLOR);
                fprintf(fp,
                        " | %s subs=%d t=%.3f/%.3f pos=(%.3f,%.3f,%.3f) scl=(%.3f,%.3f,%.3f) col=(%.2f,%.2f,%.2f,%.2f) tex=%d link=%s uv=(%.3f,%.3f,%.3f,%.3f)",
                        mi.gen->chunk.name, mi.SubCount(), su.sub.Elapsed(), mi.gen->Length(),
                        p[0], p[1], p[2], s[0], s[1], s[2], c[0], c[1], c[2], c[3],
                        mi.texIndex, mi.gen->linkName,
                        su.uv[0], su.uv[1], su.uv[2], su.uv[3]);
            }
            fprintf(fp, "\n");
        }
    }

    fprintf(fp, "\nsubtl    = %zu\n", eff.SubTimelineCount());
    fprintf(fp, "missing   = %d generator(s) unresolved\n", eff.MissingGenerators());

    //--------------------------------------------------------------
    // 再構築の検査（問題 3）: 同一 EffectContext で 2 回 Build しても
    //   テクスチャ索引が累積しないこと。
    //--------------------------------------------------------------
    const size_t tn1 = eff.TexNameCount();
    const size_t ts1 = eff.TexSpecificCount();
    const size_t g1  = eff.Generators().size();
    eff.Build(dat);
    const bool rebuildOk = (eff.TexNameCount() == tn1) &&
                           (eff.TexSpecificCount() == ts1) &&
                           (eff.TexNameCount() == eff.TexSpecificCount()) &&
                           (eff.Generators().size() == g1);
    fprintf(fp, "rebuild   = %s (texnames=%zu->%zu texspecific=%zu->%zu gen=%zu->%zu)\n",
            rebuildOk ? "ok" : "MISMATCH",
            tn1, eff.TexNameCount(), ts1, eff.TexSpecificCount(), g1, eff.Generators().size());

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

    //--------------------------------------------------------------
    // 骨格の確認（GPU 不要）: --skel <dat> [motion4]
    //--------------------------------------------------------------
    if (lpCmdLine != nullptr && strncmp(lpCmdLine, "--skel", 6) == 0)
    {
        char* p = lpCmdLine + 6;
        char path[MAX_PATH] = {};
        char mo[MAX_PATH]   = {};
        char rot[MAX_PATH]  = {};
        if (!NextToken(p, path, MAX_PATH)) return 2;
        if (!NextToken(p, mo, MAX_PATH)) mo[0] = '\0';
        if (!NextToken(p, rot, MAX_PATH)) rot[0] = '\0';

        // "human" / "0-27-82" を実パスへ（基準モデル）
        char real[MAX_PATH] = {};
        if (ResolveModelPath(path, real, MAX_PATH)) strncpy_s(path, real, _TRUNCATE);

        DatFile dat;
        if (!dat.Load(path)) { Log("[skel] load failed: %s", path); return 3; }

        Skeleton sk;
        if (!sk.Load(dat)) { Log("[skel] no skeleton (0x29): %s", path); return 4; }

        if      (_stricmp(rot, "none")    == 0) sk.SetRootMode(Skeleton::ROOT_NONE);
        else if (_stricmp(rot, "mirrory") == 0) sk.SetRootMode(Skeleton::ROOT_MIRROR_Y);
        else if (_stricmp(rot, "rotx")    == 0) sk.SetRootMode(Skeleton::ROOT_ROT_X180);
        else if (_stricmp(rot, "rotz")    == 0) sk.SetRootMode(Skeleton::ROOT_ROT_Z180);

        Log("[skel] dat=%s bones=%d motions=%zu rot=%s",
            path, sk.BoneCount(), sk.MotionCount(), sk.RootModeName());
        for (size_t i = 0; i < sk.MotionCount(); ++i)
        {
            Log("[skel] motion %-4s dur=%.4fs keys=%d",
                sk.MotionName(i), sk.MotionDuration(i), sk.MotionKeys(i));
        }

        std::vector<float> org;
        sk.Update(0.0f);
        sk.BoneOrigins(org);
        Log("[skel] rest t=0.000 root=(%.3f,%.3f,%.3f) last=(%.3f,%.3f,%.3f)",
            org[0], org[1], org[2],
            org[org.size() - 3], org[org.size() - 2], org[org.size() - 1]);
        for (int b = 0; b < 8 && b < (int)org.size() / 3; ++b)
        {
            Log("[skel] bone %2d origin=(%.3f,%.3f,%.3f)", b,
                org[b * 3], org[b * 3 + 1], org[b * 3 + 2]);
        }

        if (mo[0] != '\0')
        {
            if (!sk.SetMotion(mo))
            {
                Log("[skel] motion not found: %s", mo);
            }
            else
            {
                const float ts[3] = { 0.0f, 0.5f, 1.0f };
                for (int i = 0; i < 3; ++i)
                {
                    sk.Update(ts[i]);
                    sk.BoneOrigins(org);
                    const size_t mid = (org.size() / 6) * 3;   // 中央付近のボーン
                    Log("[skel] motion=%s t=%.3f root=(%.3f,%.3f,%.3f) mid=(%.3f,%.3f,%.3f) last=(%.3f,%.3f,%.3f)",
                        mo, ts[i],
                        org[0], org[1], org[2],
                        org[mid], org[mid + 1], org[mid + 2],
                        org[org.size() - 3], org[org.size() - 2], org[org.size() - 1]);
                }
            }
        }

        dat.Unload();
        return 0;
    }

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

    // --res WxH: 参照（AltanaView の 3D ビュー = 625x512）に解像度を合わせる。
    //   ウィンドウ生成より前に読む必要があるため、ここで先読みする。
    if (lpCmdLine != nullptr)
    {
        const char* rp = strstr(lpCmdLine, "--res");
        if (rp != nullptr)
        {
            int w = 0, h = 0;
            if (sscanf_s(rp, "--res %dx%d", &w, &h) == 2 && w >= 64 && h >= 64 && w <= 4096 && h <= 4096)
            {
                g_InitW = w;
                g_InitH = h;
            }
        }
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

    RECT rc = { 0, 0, g_InitW, g_InitH };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    g_hWnd = CreateWindowExW(WS_EX_ACCEPTFILES, kWndClass, L"EffectTest",
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             rc.right - rc.left, rc.bottom - rc.top,
                             nullptr, nullptr, hInst, nullptr);
    if (g_hWnd == nullptr) return 1;

    DragAcceptFiles(g_hWnd, TRUE);          // ★ドラッグ＆ドロップを有効化

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if (!GfxInit(g_hWnd, g_InitW, g_InitH))
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
        char model[MAX_PATH] = {};                  // --model: 骨格をエフェクトのルートに使う
        char motion[MAX_PATH] = {};
        char boneMap[MAX_PATH] = {};                // --bone-map "0:10,1:20"
        bool boneSpecified = false;                 // モデル（human）を要するか
        bool boneExplicit  = false;                 // --bone（単一骨）が明示されたか
        bool boneOnly = false;                      // --bone が明示されたか（全体ルート）
        int  frames = 90;                       // 既定 90 フレーム（1.5 s）

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
                else if (_stricmp(mode, "alpha")     == 0) { GfxSetBlendMode(GFX_BLEND_ALPHA); g_BlendAuto = false; }
                else if (_stricmp(mode, "auto")      == 0) g_BlendAuto = true;
                else { Log("[shot] unknown blend: %s", mode); return 2; }
                continue;
            }

            if (_stricmp(tok, "--bg") == 0)
            {
                char hex[MAX_PATH] = {};
                if (!NextToken(p, hex, MAX_PATH)) return 2;
                const unsigned v = (unsigned)strtoul(hex, nullptr, 16);
                g_ClearColor[0] = ((v >> 16) & 0xFF) / 255.0f;
                g_ClearColor[1] = ((v >> 8)  & 0xFF) / 255.0f;
                g_ClearColor[2] = ( v        & 0xFF) / 255.0f;
                g_ClearColor[3] = 1.0f;
                continue;
            }

            if (_stricmp(tok, "--model") == 0)
            {
                if (!NextToken(p, model, MAX_PATH)) return 2;
                continue;
            }
            if (_stricmp(tok, "--motion") == 0)
            {
                if (!NextToken(p, motion, MAX_PATH)) return 2;
                continue;
            }
            if (_stricmp(tok, "--bone") == 0)
            {
                char n[MAX_PATH] = {};
                if (!NextToken(p, n, MAX_PATH)) return 2;
                g_RootBone = atoi(n);
                boneSpecified = true;
                boneOnly = true;
                boneExplicit = true;
                continue;
            }

            if (_stricmp(tok, "--bone-map") == 0)
            {
                if (!NextToken(p, boneMap, MAX_PATH)) return 2;
                boneSpecified = true;
                continue;
            }

            if (_stricmp(tok, "--scale") == 0)
            {
                char num[MAX_PATH] = {};
                if (!NextToken(p, num, MAX_PATH)) return 2;
                const double f = atof(num);
                if (f > 0.0) g_SizeScale = (float)f;
                continue;
            }

            if (_stricmp(tok, "--size") == 0)
            {
                char mode[MAX_PATH] = {};
                if (!NextToken(p, mode, MAX_PATH)) return 2;
                if      (_stricmp(mode, "quad")   == 0) g_SizeMode = 1;
                else if (_stricmp(mode, "square") == 0) g_SizeMode = 2;
                else if (_stricmp(mode, "meter")  == 0) g_SizeMode = 3;
                else if (_stricmp(mode, "scale")  == 0) g_SizeMode = 0;
                else if (_stricmp(mode, "quadscale") == 0) g_SizeMode = 4;
                else { Log("[shot] unknown size: %s", mode); return 2; }
                continue;
            }

            if (_stricmp(tok, "--color") == 0)
            {
                char mode[MAX_PATH] = {};
                if (!NextToken(p, mode, MAX_PATH)) return 2;
                if      (_stricmp(mode, "bgr") == 0) GfxSetColorSwap(true);
                else if (_stricmp(mode, "rgb") == 0) GfxSetColorSwap(false);
                else { Log("[shot] unknown color: %s", mode); return 2; }
                continue;
            }

            if (_stricmp(tok, "--frames") == 0)
            {
                char n[MAX_PATH] = {};
                if (!NextToken(p, n, MAX_PATH)) return 2;
                frames = atoi(n);
                if (frames < 1) frames = 1;
                if (frames > 100000) frames = 100000;
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

            if (_stricmp(tok, "--life") == 0)
            {
                char mode[MAX_PATH] = {};
                if (!NextToken(p, mode, MAX_PATH)) return 2;
                if      (_stricmp(mode, "on")  == 0) g_Effect.SetLifeFromData(true);
                else if (_stricmp(mode, "off") == 0) g_Effect.SetLifeFromData(false);
                else { Log("[shot] unknown life: %s", mode); return 2; }
                continue;
            }

            if (_stricmp(tok, "--geo") == 0)
            {
                char mode[MAX_PATH] = {};
                if (!NextToken(p, mode, MAX_PATH)) return 2;
                if      (_stricmp(mode, "on")  == 0) g_Effect.SetGeo(true);
                else if (_stricmp(mode, "off") == 0) g_Effect.SetGeo(false);
                else { Log("[shot] unknown geo: %s", mode); return 2; }
                continue;
            }

            if (_stricmp(tok, "--opstat") == 0)
            {
                g_OpStat = true;
                continue;
            }

            if (_stricmp(tok, "--emit") == 0)
            {
                char mode[MAX_PATH] = {};
                if (!NextToken(p, mode, MAX_PATH)) return 2;
                if      (_stricmp(mode, "on")  == 0) { EffectSetEmitMode(true);  g_Effect.SetEmit(true);  }
                else if (_stricmp(mode, "off") == 0) { EffectSetEmitMode(false); g_Effect.SetEmit(false); }
                else { Log("[shot] unknown emit: %s", mode); return 2; }
                continue;
            }

            if (_stricmp(tok, "--zup") == 0)
            {
                char mode[MAX_PATH] = {};
                if (!NextToken(p, mode, MAX_PATH)) return 2;
                if      (_stricmp(mode, "on")  == 0) g_ZUp = true;
                else if (_stricmp(mode, "off") == 0) g_ZUp = false;
                else { Log("[shot] unknown zup: %s", mode); return 2; }
                continue;
            }

            if (_stricmp(tok, "--emitrate") == 0)
            {
                char num[MAX_PATH] = {};
                if (!NextToken(p, num, MAX_PATH)) return 2;
                EffectSetEmitRate(atoi(num));
                continue;
            }

            if (_stricmp(tok, "--layout") == 0)
            {
                char mode[MAX_PATH] = {};
                if (!NextToken(p, mode, MAX_PATH)) return 2;
                if      (_stricmp(mode, "on")  == 0) g_Layout = true;
                else if (_stricmp(mode, "off") == 0) g_Layout = false;
                else { Log("[shot] unknown layout: %s", mode); return 2; }
                continue;
            }

            if (_stricmp(tok, "--geoflat") == 0)
            {
                char mode[MAX_PATH] = {};
                if (!NextToken(p, mode, MAX_PATH)) return 2;
                if      (_stricmp(mode, "on")  == 0) g_GeoFlat = true;
                else if (_stricmp(mode, "off") == 0) g_GeoFlat = false;
                else { Log("[shot] unknown geoflat: %s", mode); return 2; }
                continue;
            }

            if (_stricmp(tok, "--attached") == 0)
            {
                char mode[MAX_PATH] = {};
                if (!NextToken(p, mode, MAX_PATH)) return 2;
                if      (_stricmp(mode, "on")  == 0) g_Effect.SetAttached(true);
                else if (_stricmp(mode, "off") == 0) g_Effect.SetAttached(false);
                else { Log("[shot] unknown attached: %s", mode); return 2; }
                continue;
            }

            if (_stricmp(tok, "--uvcrop") == 0)
            {
                char mode[MAX_PATH] = {};
                if (!NextToken(p, mode, MAX_PATH)) return 2;
                if      (_stricmp(mode, "on")  == 0) g_Effect.SetUvCrop(true);
                else if (_stricmp(mode, "off") == 0) g_Effect.SetUvCrop(false);
                else { Log("[shot] unknown uvcrop: %s", mode); return 2; }
                continue;
            }

            if (_stricmp(tok, "--uvscroll") == 0)
            {
                char mode[MAX_PATH] = {};
                if (!NextToken(p, mode, MAX_PATH)) return 2;
                if      (_stricmp(mode, "off")  == 0) g_UvScroll = 0;
                else if (_stricmp(mode, "on")   == 0) g_UvScroll = 1;   // 0..1 を巡回
                else if (_stricmp(mode, "cell") == 0) g_UvScroll = 2;   // セル内を巡回
                else { Log("[shot] unknown uvscroll: %s", mode); return 2; }
                continue;
            }

            // 解像度（ウィンドウ生成前に先読み済み。ここでは値を読み飛ばすだけ）
            if (_stricmp(tok, "--res") == 0)
            {
                char mode[MAX_PATH] = {};
                if (!NextToken(p, mode, MAX_PATH)) return 2;
                continue;
            }

            // キー補間の方式（incr = 実測どおりの増分補間 / abs = 従来の絶対補間）
            if (_stricmp(tok, "--keymode") == 0)
            {
                char mode[MAX_PATH] = {};
                if (!NextToken(p, mode, MAX_PATH)) return 2;
                if      (_stricmp(mode, "incr") == 0) EffectSetKeyIncr(true);
                else if (_stricmp(mode, "abs")  == 0) EffectSetKeyIncr(false);
                else if (_stricmp(mode, "norm") == 0) EffectSetKeyMode(2);
                else { Log("[shot] unknown keymode: %s", mode); return 2; }
                Log("[shot] keymode=%s", mode);
                continue;
            }

            // 色のゲイン（PS の ×1.5 と等価。既定 1.5）
            if (_stricmp(tok, "--gain") == 0)
            {
                char num[MAX_PATH] = {};
                if (!NextToken(p, num, MAX_PATH)) return 2;
                const double f = atof(num);
                if (f >= 0.0) g_Gain = (float)f;
                Log("[shot] gain=%.3f", g_Gain);
                continue;
            }

            // カメラ（参照動画の AltanaView.ini の値を再現するため）
            if (_stricmp(tok, "--dist") == 0)
            {
                char num[MAX_PATH] = {};
                if (!NextToken(p, num, MAX_PATH)) return 2;
                const double f = atof(num);
                if (f > 0.01) g_Dist = (float)f;
                Log("[shot] dist=%.3f", g_Dist);
                continue;
            }
            if (_stricmp(tok, "--yaw") == 0)
            {
                char num[MAX_PATH] = {};
                if (!NextToken(p, num, MAX_PATH)) return 2;
                g_Yaw = (float)atof(num);
                Log("[shot] yaw=%.4f", g_Yaw);
                continue;
            }
            if (_stricmp(tok, "--pitch") == 0)
            {
                char num[MAX_PATH] = {};
                if (!NextToken(p, num, MAX_PATH)) return 2;
                g_Pitch = (float)atof(num);
                Log("[shot] pitch=%.4f", g_Pitch);
                continue;
            }

            // 粒子の初期配置（0x06 / 0x1F）を円周上にするかの A/B。既定 on
            if (_stricmp(tok, "--ring") == 0)
            {
                char mode[MAX_PATH] = {};
                if (!NextToken(p, mode, MAX_PATH)) return 2;
                if      (_stricmp(mode, "on")  == 0) EffectSetRing(true);
                else if (_stricmp(mode, "off") == 0) EffectSetRing(false);
                else { Log("[shot] unknown ring: %s", mode); return 2; }
                continue;
            }

            // 円周半径の出どころ（0x1F）。xy = arg0/arg1（既定） / dist = |arg7|
            if (_stricmp(tok, "--ringradius") == 0)
            {
                char mode[MAX_PATH] = {};
                if (!NextToken(p, mode, MAX_PATH)) return 2;
                if      (_stricmp(mode, "xy")   == 0) EffectSetRingDist(false);
                else if (_stricmp(mode, "dist") == 0) EffectSetRingDist(true);
                else { Log("[shot] unknown ringradius: %s", mode); return 2; }
                continue;
            }

            // 配置の形: line（旧）/ ring（円周に均等）/ spiral（らせん）
            if (_stricmp(tok, "--arrange") == 0)
            {
                char mode[MAX_PATH] = {};
                if (!NextToken(p, mode, MAX_PATH)) return 2;
                if      (_stricmp(mode, "line")   == 0) EffectSetArrange(0);
                else if (_stricmp(mode, "ring")   == 0) EffectSetArrange(1);
                else if (_stricmp(mode, "spiral") == 0) EffectSetArrange(2);
                else { Log("[shot] unknown arrange: %s", mode); return 2; }
                Log("[shot] arrange=%s", mode);
                continue;
            }

            // 円周を張る面: xy（既定）/ xz / yz
            if (_stricmp(tok, "--ringplane") == 0)
            {
                char mode[MAX_PATH] = {};
                if (!NextToken(p, mode, MAX_PATH)) return 2;
                if      (_stricmp(mode, "xy") == 0) EffectSetRingPlane(0);
                else if (_stricmp(mode, "xz") == 0) EffectSetRingPlane(1);
                else if (_stricmp(mode, "yz") == 0) EffectSetRingPlane(2);
                else { Log("[shot] unknown ringplane: %s", mode); return 2; }
                continue;
            }

            // らせんの巻き数
            if (_stricmp(tok, "--spiralturns") == 0)
            {
                char num[MAX_PATH] = {};
                if (!NextToken(p, num, MAX_PATH)) return 2;
                const double f = atof(num);
                if (f > 0.0) EffectSetSpiralTurns((float)f);
                continue;
            }

            // 円周半径の倍率（データの 0.1..0.5 m はビルボードより小さいため、確認用）
            if (_stricmp(tok, "--ringscale") == 0)
            {
                char num[MAX_PATH] = {};
                if (!NextToken(p, num, MAX_PATH)) return 2;
                const double f = atof(num);
                if (f > 0.0) EffectSetRingScale((float)f);
                Log("[shot] ringscale=%.3f ring=%d", (float)f, EffectRing() ? 1 : 0);
                continue;
            }

            // 色/α の代替処理（RGB=0→白 / α≤0→1）の A/B（問題 7）。既定 on
            if (_stricmp(tok, "--colorfallback") == 0)
            {
                char mode[MAX_PATH] = {};
                if (!NextToken(p, mode, MAX_PATH)) return 2;
                if      (_stricmp(mode, "on")  == 0) g_ColorFallback = true;
                else if (_stricmp(mode, "off") == 0) g_ColorFallback = false;
                else { Log("[shot] unknown colorfallback: %s", mode); return 2; }
                continue;
            }

            // Move(0x8C) の位置積分（AltanaView の pos += move*dt）。既定 on
            if (_stricmp(tok, "--moveint") == 0)
            {
                char mode[MAX_PATH] = {};
                if (!NextToken(p, mode, MAX_PATH)) return 2;
                if      (_stricmp(mode, "on")  == 0) EffectSetMoveInt(true);
                else if (_stricmp(mode, "off") == 0) EffectSetMoveInt(false);
                else { Log("[shot] unknown moveint: %s", mode); return 2; }
                Log("[shot] moveint=%s", mode);
                continue;
            }

            // 面内回転（VM#1 0x3D/0x3E/0x40, VM#2 0x29/0x2B）。既定 off
            if (_stricmp(tok, "--spin") == 0)
            {
                char mode[MAX_PATH] = {};
                if (!NextToken(p, mode, MAX_PATH)) return 2;
                if      (_stricmp(mode, "on")  == 0) EffectSetSpin(true);
                else if (_stricmp(mode, "off") == 0) EffectSetSpin(false);
                else { Log("[shot] unknown spin: %s", mode); return 2; }
                Log("[shot] spin=%s", mode);
                continue;
            }

            // VM#1 0x3A の散布（意味未確定）。既定 off
            if (_stricmp(tok, "--scatter") == 0)
            {
                char mode[MAX_PATH] = {};
                if (!NextToken(p, mode, MAX_PATH)) return 2;
                if      (_stricmp(mode, "on")  == 0) EffectSetScatter(true);
                else if (_stricmp(mode, "off") == 0) EffectSetScatter(false);
                else { Log("[shot] unknown scatter: %s", mode); return 2; }
                Log("[shot] scatter=%s", mode);
                continue;
            }

            // ffxitool5 実測に合わせた「参照モード」を一括適用
            //   加算合成(SRCALPHA+ONE) / RGB×4 / キー補間 norm / サイズ=VM Scale /
            //   Move 積分 / 0x1F の半径倍率 1
            if (_stricmp(tok, "--ffxi") == 0)
            {
                char mode[MAX_PATH] = {};
                if (!NextToken(p, mode, MAX_PATH)) return 2;
                if (_stricmp(mode, "on") == 0)
                {
                    GfxSetBlendMode(GFX_BLEND_ALPHA_ADD);
                    g_BlendAuto = false;
                    g_Gain = 4.0f;
                    g_SizeMode = 0;                 // --size scale
                    EffectSetKeyMode(2);            // --keymode norm
                    EffectSetMoveInt(true);         // --moveint on
                    EffectSetRingScale(1.0f);       // --ringscale 1
                    Log("[shot] ffxi=on (alpha_add/gain4/norm/scale/moveint/ringscale1)");
                }
                else if (_stricmp(mode, "off") == 0)
                {
                    GfxSetBlendMode(GFX_BLEND_ALPHA);
                    g_BlendAuto = false;
                    g_Gain = 1.5f;
                    g_SizeMode = 3;
                    EffectSetKeyMode(0);
                    EffectSetMoveInt(false);
                    EffectSetRingScale(4.0f);
                    Log("[shot] ffxi=off");
                }
                else { Log("[shot] unknown ffxi: %s", mode); return 2; }
                continue;
            }

            if (_stricmp(tok, "--trace") == 0)
            {
                // サブ（粒子）ごとの状態を CSV に出す（GUI の T キーと同じ形式）
                if (g_TraceFile == nullptr) fopen_s(&g_TraceFile, "effecttest_trace.csv", "wb");
                if (g_TraceFile != nullptr)
                    fprintf(g_TraceFile, "frame,instance,sub,name,elapsed,t01,px,py,pz,sx,sy,sz,r,g,b,a\n");
                continue;
            }

            if (_stricmp(tok, "--uvframe") == 0)
            {
                char mode[MAX_PATH] = {};
                if (!NextToken(p, mode, MAX_PATH)) return 2;
                if      (_stricmp(mode, "random") == 0) g_Effect.SetRandomFrame(true);
                else if (_stricmp(mode, "time")   == 0) g_Effect.SetRandomFrame(false);
                else { Log("[shot] unknown uvframe: %s", mode); return 2; }
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

        //--------------------------------------------------------------
        // 骨格をエフェクトのルート変換として使う場合（--model / --motion / --bone）
        //--------------------------------------------------------------
        DatFile  mdl;
        Skeleton skel;
        bool     haveSkel = false;

        if (model[0] == '\0' && boneSpecified)
            strncpy_s(model, "human", _TRUNCATE);       // --bone だけなら基準モデル（human）を使う

        // モデルに載せる場合の既定は「部位 ID から骨を決める」（--bone 単一指定が無いとき）
        //   データ2 op 0x01 の +0x06（部位 ID）→ 骨。無モデル時のゴールデンには影響しない
        char boneMapUse[MAX_PATH] = {};
        strncpy_s(boneMapUse, boneMap, _TRUNCATE);
        if (boneMapUse[0] == '\0' && !boneExplicit && model[0] != '\0')
            strncpy_s(boneMapUse, "part", _TRUNCATE);

        char mpath[MAX_PATH] = {};
        if (model[0] != '\0')
        {
            if (!ResolveModelPath(model, mpath, MAX_PATH))
            {
                Log("[shot] model resolve failed: %s", model);
            }
            else if (mdl.Load(mpath) && skel.Load(mdl))
            {
                haveSkel = true;
                // AltanaView の ctx+0x1a0 != 0（モデル従属）相当:
                //   モデルに載せる場合は VM#1 の位置 op を実行しない
                g_Effect.SetAttached(true);
                if (motion[0] != '\0' && !skel.SetMotion(motion))
                    Log("[shot] model motion not found: %s", motion);
                Log("[shot] model=%s bones=%d motions=%zu motion=%s rot=%s bone=%d",
                    mpath, skel.BoneCount(), skel.MotionCount(),
                    (motion[0] != '\0') ? motion : "(rest)", skel.RootModeName(), g_RootBone);
            }
            else
            {
                Log("[shot] model load failed: %s", mpath);
            }
        }

        for (int i = 0; i < frames; ++i)
        {
            g_Effect.Advance(1.0f / 60.0f);

            if (haveSkel)
            {
                skel.Update((float)(i + 1) / 60.0f);
                const float* w = skel.World(g_RootBone);
                if (w != nullptr)
                {
                    memcpy(g_Root, w, sizeof(g_Root));
                    if (boneOnly) g_RootActive = true;   // --bone 指定時のみ全体に適用
                }
            }
        }

        if (g_RootActive)
            Log("[shot] root bone=%d pos=(%.3f,%.3f,%.3f)",
                g_RootBone, g_Root[12], g_Root[13], g_Root[14]);

        //--------------------------------------------------------------
        // インスタンス毎の骨（--bone-map "i:b,i:b"）。骨は 0 始まりの配列番号。
        //--------------------------------------------------------------
        if (haveSkel && boneMap[0] != '\0')
        {
            const size_t n = g_Effect.Instances().size();
            g_InstMats.assign(n * 16, 0.0f);
            g_InstHasBone.assign(n, 0);

            const char* s = boneMapUse;
            if (_stricmp(boneMapUse, "part") == 0)
            {
                // 部位 ID（op 0x01 の +0x06）→ 骨を推定して割り当てる
                //   参考: 0x001/0x011=中心, 0x021=胴, 0x081=右腕, 0x091=左腕,
                //         0x0A1=右足, 0x0B1=左足, 0x0C1=頭 …
                //   骨名が無いため Y（高さ）と X（左右）で最近傍を選ぶ
                auto pickBone = [&](int partId) -> int
                {
                    const int zones = partId >> 4;      // 0..0x14, 0x20
                    float ty = 1.00f;                   // 目標の高さ
                    float tx = 0.0f;                    // 目標の左右（+右 / -左）
                    if (zones <= 1)                    { ty = 1.00f; tx = 0.00f; }   // 中心
                    else if (zones == 2)               { ty = 1.05f; tx = 0.00f; }   // 胴
                    else if (zones == 3)               { ty = 0.95f; tx = 0.00f; }
                    else if (zones == 4)               { ty = 1.10f; tx = 0.00f; }   // 胴前面
                    else if (zones == 7)               { ty = 1.00f; tx = 0.00f; }   // 手前
                    else if (zones == 8)               { ty = 1.35f; tx = 0.22f; }   // 右腕
                    else if (zones == 9)               { ty = 1.35f; tx = -0.22f; }  // 左腕
                    else if (zones == 10)              { ty = 0.45f; tx = 0.10f; }   // 右足
                    else if (zones == 11)              { ty = 0.45f; tx = -0.10f; }  // 左足
                    else if (zones == 12)              { ty = 1.65f; tx = 0.00f; }   // 頭
                    else if (zones == 13 || zones == 14) { ty = 1.30f; tx = 0.22f; } // 腕付近
                    else if (zones == 15)              { ty = 1.20f; tx = -0.22f; }
                    else if (zones == 16)              { ty = 0.40f; tx = 0.10f; }
                    else if (zones == 17)              { ty = 0.25f; tx = 0.00f; }   // 足元
                    else if (zones == 18)              { ty = 0.45f; tx = -0.10f; }
                    else if (zones == 19)              { ty = 0.60f; tx = -0.10f; }
                    else if (zones == 20)              { ty = 0.40f; tx = 0.10f; }
                    else if (zones == 32)              { ty = 1.20f; tx = 0.25f; }   // 0x201 右手前

                    int best = -1;
                    float bestD = 1e9f;
                    for (int b = 0; b < skel.BoneCount(); ++b)
                    {
                        const float* w = skel.World(b);
                        if (w == nullptr) continue;
                        const float dx = w[12] - tx;
                        const float dy = w[13] - ty;
                        const float d = dx * dx + dy * dy;
                        if (d < bestD) { bestD = d; best = b; }
                    }
                    return best;
                };

                for (size_t i = 0; i < n; ++i)
                {
                    const int pid = g_Effect.Instances()[i].gen->partId;
                    const int b = pickBone(pid >= 0 ? pid : 0x001);
                    if (b < 0) continue;
                    const float* w = skel.World(b);
                    if (w == nullptr) continue;
                    memcpy(&g_InstMats[i * 16], w, 16 * sizeof(float));
                    g_InstHasBone[i] = 1;
                    Log("[shot] part inst=%zu chunk=%s partId=0x%03X zone=%d -> bone=%d",
                        i, g_Effect.Instances()[i].gen->chunk.name, pid, (pid >> 4), b);
                }
            }
            else if (_stricmp(boneMapUse, "auto") == 0)
            {
                // 体軸（Y）に沿って均等に骨を割り当てる（根拠: 参照動画は体の各所に炎が付く）
                //   0.05〜1.75 m の範囲に入る骨を Y で並べ、インスタンス数で等分する
                const int bones = skel.BoneCount();
                std::vector<int> cand;
                for (int b = 0; b < bones; ++b)
                {
                    const float* w = skel.World(b);
                    if (w == nullptr) continue;
                    const float y = w[13];
                    if (y >= 0.05f && y <= 1.75f) cand.push_back(b);
                }
                if (!cand.empty())
                {
                    std::sort(cand.begin(), cand.end(), [&](int a, int b) {
                        const float* wa = skel.World(a);
                        const float* wb = skel.World(b);
                        return (wa != nullptr && wb != nullptr) ? (wa[13] < wb[13]) : (a < b);
                    });
                    for (size_t i = 0; i < n && i < (size_t)100; ++i)
                    {
                        const int pick = cand[(i * cand.size()) / (n > 0 ? n : 1) % cand.size()];
                        const float* w = skel.World(pick);
                        if (w == nullptr) continue;
                        memcpy(&g_InstMats[i * 16], w, 16 * sizeof(float));
                        g_InstHasBone[i] = 1;
                    }
                    Log("[shot] bone-map=auto bones=%d cand=%zu", bones, cand.size());
                }
            }
            else
            while (*s != '\0')
            {
                const int idx = atoi(s);
                const char* c = strchr(s, ':');
                if (c == nullptr) break;

                const int bone = atoi(c + 1);
                if (idx >= 0 && (size_t)idx < n)
                {
                    const float* w = skel.World(bone);
                    if (w != nullptr)
                    {
                        memcpy(&g_InstMats[(size_t)idx * 16], w, 16 * sizeof(float));
                        g_InstHasBone[(size_t)idx] = 1;
                    }
                }

                const char* comma = strchr(c + 1, ',');
                if (comma == nullptr) break;
                s = comma + 1;
            }

            int mapped = 0;
            for (char f : g_InstHasBone) if (f) ++mapped;
            Log("[shot] bone-map=%s mapped=%d inst=%zu", boneMapUse, mapped, n);
        }

        // 描画入力のダンプ（原因切り分け用）
        for (const TimTexture& t : g_Effect.Textures())
            Log("[shot] tex %s %ux%u uvb=(%.3f,%.3f,%.3f,%.3f) black=%.3f srv=%p", t.name.c_str(), t.width, t.height,
                t.uvb[0], t.uvb[1], t.uvb[2], t.uvb[3], t.blackRatio, (void*)t.srv);

        for (const MotionInstance& mi : g_Effect.Instances())
        {
            const SubUnit& su = mi.subs[0];
            const float* pp = su.sub.At(SUB_POS);
            const float* ss = su.sub.At(SUB_SCALE);
            const float* cc = su.sub.At(SUB_COLOR);
            Log("[shot] inst %s subs=%d bil=%d pos=(%.2f,%.2f,%.2f) scl=(%.2f,%.2f,%.2f) col=(%.2f,%.2f,%.2f,%.2f) tex=%d link=%s uv=(%.3f,%.3f,%.3f,%.3f) frm=%d/%zu geo=%d flat=%d nq=%zu half=(%.3f,%.3f) ctr=(%.3f,%.3f) srv=%p",
                mi.gen->chunk.name, mi.SubCount(), mi.gen->bil,
                pp[0], pp[1], pp[2], ss[0], ss[1], ss[2],
                cc[0], cc[1], cc[2], cc[3], mi.texIndex, mi.gen->linkName,
                su.uv[0], su.uv[1], su.uv[2], su.uv[3], su.frameIdx, mi.gen->texFrames.size(),
                mi.gen->hasGeo ? 1 : 0, mi.gen->geoFlat ? 1 : 0, mi.gen->geoQuads.size(),
                mi.gen->geoHalf[0], mi.gen->geoHalf[1],
                mi.gen->geoCenter[0], mi.gen->geoCenter[1], (void*)mi.srv);
        }

        RenderScene(false);                     // Present せずに描画

        wchar_t wpng[MAX_PATH] = {};
        MultiByteToWideChar(CP_ACP, 0, png, -1, wpng, MAX_PATH);
        const bool ok = GfxSaveBackBufferPNG(wpng);

        // --opstat: 実際に実行された VM op の集計（VM#1 が生成時のみ走るため重要）
        if (g_OpStat)
        {
            for (int vm = 1; vm <= 2; ++vm)
            {
                const std::vector<OpCount> st = EffectOpStat(vm);
                if (st.empty()) continue;
                Log("[opstat] VM#%d : %zu kinds", vm, st.size());
                for (const OpCount& oc : st)
                    Log("[opstat]   op 0x%02X x%d", oc.op, oc.count);
            }
        }

        Log("[shot] dat=%s png=%s blend=%s uvmode=%s color=%s size=%s frames=%d inst=%zu tex=%zu ok=%d",
            dat, png, GfxBlendModeName(GfxGetBlendMode()),
            g_Effect.UseTexFrames() ? "0x21" : "full",
            GfxGetColorSwap() ? "bgr" : "rgb",
            g_SizeMode == 1 ? "quad" : (g_SizeMode == 2 ? "square" : (g_SizeMode == 3 ? "meter" : "scale")), frames,
            g_Effect.Instances().size(), g_Effect.Textures().size(), ok ? 1 : 0);

        g_Effect.ReleaseTextures();
        g_Dat.Unload();
        g_Base.Unload();
        GfxShutdown();
        if (g_TraceFile != nullptr) { fclose(g_TraceFile); g_TraceFile = nullptr; }
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




