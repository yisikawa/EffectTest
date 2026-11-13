//======================================================================
//  EffectVm.cpp : 2 段 VM（AltanaView FUN_0040e650 / FUN_0040f4b0 の移植）
//
//  命令語レイアウト（両 VM 共通）
//      dword0 = [op:8][len:8][?:3][dest:13..]   dest = dword0 >> 13
//      以降   = float オペランド
//      次命令 = 現在位置 + (byte[1] & 0x1F) * 4
//
//  op の網羅性は各 case のコメントを参照。未確定のものは「近似」または
//  「no-op（IP のみ前進）」として明示している。
//======================================================================
#include "Effect.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

namespace
{
    // キーフレーム表の線形補間（AltanaView FUN_0040f820 の移植・簡略版）
    //   表は 0x19 チャンクの +0x18 から 8 バイト単位の [time, value]。
    //   time は t01（0..1）の領域。count 件だけ読み、チャンク外は読まない。
    //   先頭キーが 0 より後（例 0.225）のときは、最初の区間の傾きで手前を外挿する。
    float EvalKeyTrack(const DatFile& dat, uint32_t first, uint32_t count, float t01, float scale)
    {
        if (first == 0u || count == 0u) return 0.0f;
        if (count > 64u) count = 64u;

        float prevT = 0.0f, prevV = 0.0f;
        bool  have  = false;
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t e = first + i * 8u;
            if (!dat.InRange(e, 8)) break;
            const uint8_t* q = dat.At(e);
            float t = 0.0f, v = 0.0f;
            memcpy(&t, q + 0, 4);
            memcpy(&v, q + 4, 4);
            if (t < -1e6f || t > 1e6f || v < -1e12f || v > 1e12f) break;   // 異常値は打ち切り

            if (!have) { prevT = t; prevV = v; have = true; continue; }
            if (t01 <= t)
            {
                const float dt = t - prevT;
                if (dt <= 1e-6f) return scale * v;
                return scale * (prevV + (t01 - prevT) * (v - prevV) / dt);
            }
            prevT = t; prevV = v;
        }
        return have ? (scale * prevV) : 0.0f;
    }

    // キー補間（実測 FUN_0040f820 の移植・2026-11-12 逆コンパイルで確定）
    //   if (tOld == tNew || cursor 無効) → 何もしない
    //   tNew <= 現在キー時刻:
    //       out += (tNew - tOld) * (scale*keyValue - out) / (keyTime - tOld)   ← 現在値基準
    //   tNew > 現在キー時刻:
    //       カーソルを「次のキー時刻 >= tNew」まで前進し、通過した最後のキー値を base にして
    //       out = (tNew - keyTimeBase) * (scale*keyValueNext - base) / (keyTimeNext - keyTimeBase) + base
    //   ※ 元実装は「前進した場合はキー間の絶対補間、しなかった場合は現在値基準」という非対称。
    float EvalKeyTrackIncr(const DatFile& dat, uint32_t first, uint32_t count,
                           float tOld, float tNew, float scale,
                           uint32_t& cursor, float out)
    {
        if (first == 0u || count == 0u) return out;
        if (tOld == tNew) return out;
        if (count > 64u) count = 64u;
        if (cursor >= count) cursor = count - 1;

        float kt = 0.0f, kv = 0.0f;
        const uint32_t ec = first + cursor * 8u;
        if (!dat.InRange(ec, 8)) return out;
        {
            const uint8_t* q = dat.At(ec);
            memcpy(&kt, q, 4);
            memcpy(&kv, q + 4, 4);
        }
        if (kt < -1e6f || kt > 1e6f || kv < -1e12f || kv > 1e12f) return out;

        if (tNew > kt)
        {
            // 前進: 通過した最後のキー（base）を覚えつつ、次のキーまで進む
            float baseT = kt, baseV = scale * kv;
            while (cursor + 1u < count)
            {
                const uint32_t en = first + (cursor + 1u) * 8u;
                if (!dat.InRange(en, 8)) break;
                const uint8_t* qn = dat.At(en);
                float tn = 0.0f, vn = 0.0f;
                memcpy(&tn, qn, 4); memcpy(&vn, qn + 4, 4);
                if (!(tNew > tn)) break;             // tNew <= 次キー時刻 で停止
                ++cursor;
                baseT = tn; baseV = scale * vn;
            }
            const uint32_t ec2 = first + cursor * 8u;
            if (!dat.InRange(ec2, 8)) return out;
            const uint8_t* q2 = dat.At(ec2);
            float kt2 = 0.0f, kv2 = 0.0f;
            memcpy(&kt2, q2, 4); memcpy(&kv2, q2 + 4, 4);
            if (kt2 < -1e6f || kt2 > 1e6f || kv2 < -1e12f || kv2 > 1e12f) return out;

            const float denom = kt2 - baseT;
            if (fabsf(denom) < 1e-6f) return scale * kv2;
            return baseV + (tNew - baseT) * (scale * kv2 - baseV) / denom;
        }

        // 前進なし: 現在値を基準に tOld→tNew の割合だけ寄せる
        const float denom = kt - tOld;
        if (fabsf(denom) < 1e-6f) return scale * kv;
        return out + (tNew - tOld) * (scale * kv - out) / denom;
    }

    // VM#1 のキー表設定 op（実測 FUN_0040e0b0 L11030-11037）
    //   `(0x20<op<0x30) || (0x32<op<0x37) || (0x58<op<0x5c)`
    //   VM#1 の保存判定と --selftest の監査で共用する。
    inline bool IsKeyOp(uint8_t op)
    {
        return (op > 0x20 && op < 0x30) || (op > 0x32 && op < 0x37) || (op > 0x58 && op < 0x5C);
    }

    inline float Rand01() { return (float)rand() / (float)RAND_MAX; }
    // 0..255 の乱数（AltanaView FUN_0044c55c。色バイトやジッタ量に使われる）
    inline int RandByte() { return (int)(Rand01() * 256.0f) & 0xFF; }
    inline uint32_t InstLen(const uint8_t* p) { return (uint32_t)(p[1] & 0x1F); }

    bool g_EmitMode = true;
    int  g_EmitRate = 24;          // 既定の放出レート（個/秒）。--emitrate で変更
    bool g_Ring     = true;        // 0x06/0x1F を円周上配置にする（--ring on/off）
    bool g_KeyIncr  = true;        // キー補間を実測どおり「カーソル前進＋現在値基準」で行う（--keymode abs で旧方式）
    bool g_RingDist = false;       // 0x1F の半径に arg7（-0.4..-0.8 の絶対値）を使う
                                   //   （--ringradius dist。既定 xy = arg0/arg1）
    float g_RingScale = 4.0f;      // 円周半径の倍率（--ringscale N。既定 4）
                                   //   引数の半径 (0.1..0.5 m) は「サブ粒子スケール」で、
                                   //   ビルボード（2..4 m）より小さいため、そのままでは
                                   //   円周が埋もれて直線に見える。4 倍でリングとして見える。
    int   g_Arrange  = 1;          // 0=line（旧）/ 1=ring（円周・均等）/ 2=spiral（らせん）
    int   g_Plane    = 0;          // 0=xy / 1=xz / 2=yz（円周を張る面）
    float g_SpiralTurns = 1.0f;    // らせんの巻き数（--spiralturns）
    bool  g_MoveInt  = false;      // Move(0x8C) を pos に積分する（--moveint）。既定 off
                                   //   根拠は Cure 10/9 で確認済み（pos が散る）。
                                   //   既存ゴールデン (11\17=9310 / 11\22=962 / 12\43=4764)
                                   //   は on にすると大きく変わるため、参照画像で裏付けが
                                   //   取れるまで既定 off（--moveint on で A/B）。
    bool  g_Spin     = false;      // 面内回転 0x3D/0x3E/0x40・0x29/0x2B（--spin）。既定 off
    bool  g_Scatter  = false;      // VM#1 0x3A の散布（--scatter）。既定 off（意味未確定）
    int  g_OpStat[3][256] = {};    // [vm][op] の実行回数（--opstat）

    //------------------------------------------------------------------
    // 円周（楕円）／らせん上への配置（0x06 / 0x1F が使う）
    //   AltanaView の 0x06 / 0x07 / 0x1F は「円運動＋乱数位相」。
    //   **乱数だとムラが出て線に見える**ため、粒子番号から黄金角で
    //   均等に割り付ける（何個出ても円周を均等に覆う）。
    //     ring  : φ = 2π·frac(i·1/φ)          軸方向のずらし無し
    //     spiral: 同じ φ に軸方向のずらしを加える（らせん）
    //   半径は引数の (rX, rY)、面は --ringplane、倍率は --ringscale。
    //------------------------------------------------------------------
    inline void PlaceOnRing(SubObject& sub, float rX, float rY, int idx)
    {
        const float kGolden = 0.6180339887498949f;          // 1/φ（黄金比の逆数）
        float u = (float)idx * kGolden;
        u -= floorf(u);                                     // 0..1（低食い違い＝均等）

        float phi = 6.283185307179586f * u;
        if (g_Arrange == 2) phi *= g_SpiralTurns;

        const float x = cosf(phi) * rX * g_RingScale;
        const float y = sinf(phi) * rY * g_RingScale;

        // らせん: 位相と同じ配分で軸方向にずらす（1 巻きで 1 ピッチ）
        float axial = 0.0f;
        if (g_Arrange == 2)
            axial = (u - 0.5f) * 2.0f * (rX + rY) * g_RingScale;

        float* p = sub.Pos();
        switch (g_Plane)
        {
        case 1:  p[0] += x; p[2] += y; p[1] += axial; break;   // xz 面
        case 2:  p[1] += x; p[2] += y; p[0] += axial; break;   // yz 面
        default: p[0] += x; p[1] += y; p[2] += axial; break;   // xy 面
        }
    }
}

void EffectSetEmitMode(bool on) { g_EmitMode = on; }
bool EffectEmitMode() { return g_EmitMode; }
void EffectSetEmitRate(int n) { g_EmitRate = (n > 0) ? n : 1; }
void EffectSetRing(bool on) { g_Ring = on; g_Arrange = on ? 1 : 0; }
bool EffectRing() { return g_Ring; }
void EffectSetRingScale(float s) { g_RingScale = (s > 0.0f) ? s : 1.0f; }
void EffectSetRingDist(bool on) { g_RingDist = on; }
bool EffectRingDist() { return g_RingDist; }
void EffectSetKeyIncr(bool on) { g_KeyIncr = on; }
bool EffectKeyIncr() { return g_KeyIncr; }
void EffectSetArrange(int mode)
{
    g_Arrange = (mode >= 0 && mode <= 2) ? mode : 1;
    g_Ring    = (g_Arrange != 0);
}
int  EffectArrange() { return g_Arrange; }
void EffectSetRingPlane(int plane) { g_Plane = (plane >= 0 && plane <= 2) ? plane : 0; }
int  EffectRingPlane() { return g_Plane; }
void EffectSetSpiralTurns(float t) { g_SpiralTurns = (t > 0.0f) ? t : 1.0f; }
void EffectSetMoveInt(bool on) { g_MoveInt = on; }
bool EffectMoveInt() { return g_MoveInt; }
void EffectSetSpin(bool on) { g_Spin = on; }
bool EffectSpin() { return g_Spin; }
void EffectSetScatter(bool on) { g_Scatter = on; }
bool EffectScatter() { return g_Scatter; }


void EffectOpStatReset()
{
    for (int v = 0; v < 3; ++v)
        for (int i = 0; i < 256; ++i) g_OpStat[v][i] = 0;
}

std::vector<OpCount> EffectOpStat(int vm)
{
    std::vector<OpCount> out;
    if (vm < 1 || vm > 2) return out;
    for (int i = 0; i < 256; ++i)
        if (g_OpStat[vm][i] > 0) out.push_back(OpCount{ i, g_OpStat[vm][i] });
    std::sort(out.begin(), out.end(),
              [](const OpCount& a, const OpCount& b) { return a.count > b.count; });
    return out;
}

//======================================================================
//  VM#1 : 目標値評価（FUN_0040e650）
//======================================================================
static void Vm1(const MotionInstance& mi, SubUnit& su, const EffectContext& ctx)
{
    // モデル従属モード（AltanaView の ctx+0x1a0 != 0）:
    //   実測 FUN_0040e650 では「ctx+0x1a0 == 0 かつ (ctx+0x1a8 & 0x800000) == 0」のときだけ
    //   ctx+0x60 の 16 dword（行列）を粒子へコピーする。**VM 自体は停止しない**。
    //   （旧実装は here で return していたため、モデルを出すと効果が消えていた）
    //   → 行列コピーは当方では行わない（骨行列を描画側で中心に適用）ので、何もしない。

    uint32_t ip = mi.gen->tblB;

    for (int guard = 0; guard < 8192; ++guard)
    {
        const uint8_t* p = mi.dat->At(ip);
        if (p == nullptr || !mi.dat->InRange(ip, 8)) break;

        uint32_t w0 = 0;
        memcpy(&w0, p, 4);
        const uint8_t op = (uint8_t)(w0 & 0xFF);
        if (op == 0) break;                       // 終端
        ++g_OpStat[1][op];                        // --opstat 用

        const float* arg = reinterpret_cast<const float*>(p + 4);
        const uint32_t dest = w0 >> 13;
        // 問題 5: Dest() を返す op は最大 3 float を書く。`dest + 4` で判定すれば
        //   f[] の末尾を越えない（static_assert で保証）。
        float* d = (dest + 4u <= SUB_DEST_N) ? su.sub.Dest(dest) : nullptr;

        //--- キーフレーム表の参照（実測 FUN_0040e0b0 L11030-11037）
        //   `(0x20<op<0x30) || (0x32<op<0x37) || (0x58<op<0x5c)`
        //   → particle[dest+0x43] = 命令の 3 番目の dword
        //   **第 3 dword はファイルオフセットではなく 0x19 チャンク名**（"k006" 等 /
        //   実測 27/27）。ここで名前解決し、表の先頭と件数だけを保持する（問題 1）。
        if (IsKeyOp(op))
        {
            if (dest < SUB_DEST_N)
            {
                char nm[5] = {};
                memcpy(nm, p + 8, 4);
                const DatChunk* kc = mi.dat->Find(nm, 0x19);
                if (kc != nullptr && kc->size > 0x18)
                {
                    const uint32_t n = (kc->size - 0x18u) / 8u;   // エントリ数（ヘッダに件数は無い）
                    su.keyFirst[dest] = kc->offset + 0x18u;
                    su.keyCount[dest] = (uint8_t)((n > 64u) ? 64u : n);
                }
                else
                {
                    su.keyFirst[dest] = 0;
                    su.keyCount[dest] = 0;      // 未解決 → VM#2 はスロットを書かない
                }
            }
        }
        else
        switch (op)
        {
        //--- 定数 float3 → 出力スロット ---------------------------------
        case 0x02:
        case 0x0B:
        case 0x12:
            if (d) { d[0] = arg[0]; d[1] = arg[1]; d[2] = arg[2]; }
            break;

        //--- 定数 float3 → MOVE スロット（実測 FUN_0040e650: param_2[0x23..0x25]）
        case 0x09:
        {
            float* t = su.sub.Move();
            t[0] = arg[0]; t[1] = arg[1]; t[2] = arg[2];
            break;
        }

        //--- 定数 float3 → SCALE スロット（実測: param_2[0x26..0x28] = 0x98 = Scale）
        case 0x0F:
        {
            float* t = su.sub.Scale();
            t[0] = arg[0]; t[1] = arg[1]; t[2] = arg[2];
            break;
        }

        //--- 位置に加算 / スケールに乗算 --------------------------------
        case 0x47:
        {
            float* t = su.sub.Pos();
            t[0] += arg[0]; t[1] += arg[1]; t[2] += arg[2];
            break;
        }
        case 0x49:
        {
            float* t = su.sub.Scale();
            t[0] *= arg[0]; t[1] *= arg[1]; t[2] *= arg[2];
            break;
        }

        //--- RGBA（4 バイト）→ 色 --------------------------------------
        case 0x16:
        {
            const uint8_t* b = p + 4;
            float* c = su.sub.Color();
            c[0] = b[0] / 255.0f;
            c[1] = b[1] / 255.0f;
            c[2] = b[2] / 255.0f;
            c[3] = b[3] / 255.0f;
            break;
        }

        //--- 生成位置のばらつき（実測 FUN_0040e650 op 0x06 / 0x40E9B0-0x40EA00）
        //   AltanaView の 0x06/0x07/0x1F は「円運動＋乱数位相」。
        //   引数 (arg0, arg1) を XY 平面の楕円半径とし、位相だけ乱数にする
        //   → 粒子は**円周上**に配置される（半径は全粒子で一定）。
        //   --ring off では旧実装（球面乱数）を残す（A/B 用）。
        //     旧: mag = arg1 + arg0*rand / phi = rand*2π / theta = rand*π − π/2
        //         pos += (cosθcosφ, sinθ, cosθsinφ) * mag
        case 0x06:
        {
            if (g_Ring)
            {
                PlaceOnRing(su.sub, arg[0], arg[1], mi.emitSpawned);
                break;
            }

            static const float kPi = 3.14159265358979f;
            const float mag   = arg[1] + arg[0] * Rand01();
            const float phi   = Rand01() * 2.0f * kPi;
            const float theta = Rand01() * kPi - 0.5f * kPi;
            const float ct = cosf(theta), st = sinf(theta);
            float* t = su.sub.Pos();
            t[0] += ct * cosf(phi) * mag;
            t[1] += st * mag;
            t[2] += ct * sinf(phi) * mag;
            break;
        }

        //--- 円運動（実測 FUN_0040e650 case 0x1F。2026-11-12 逆コンパイルで確定）
        //   引数（arg0..arg10、11 個）:
        //     arg1       : 位相の基底（rand × dest 値 × 1/65536 + arg1）
        //     arg2..4    : 方向ベクトル（回転対象）
        //     arg5, arg6 : 行列の静的回転角（thunk_0042ed05 / 0x42ec62 = D3D 回転）
        //     arg7 ± arg8: 2 番目の角度（乱数レンジ）
        //     arg9       : 1 のとき条件付きで 2 回適用
        //     arg10      : >0 のとき方位を「粒子番号で均等」に割る（全周に散らす）
        //   → 粒子を**円錐状に散布**する（円周上に等間隔で並べる op ではない）
        case 0x1F:
        {
            if (!g_Ring) break;                     // --arrange line では旧挙動（no-op）

            const float vx0 = arg[2], vy0 = arg[3], vz0 = arg[4];
            if (fabsf(vx0) + fabsf(vy0) + fabsf(vz0) < 1e-6f) break;

            // 位相（実測 FUN_0040e650 case 0x1F の実アセンブリ）
            //   A = rand × arg0 × 1/65536 + arg1     （※ arg0 であって dest 値ではない）
            const float phase = arg[0] * (1.0f / 65536.0f) * Rand01() + arg[1];

            // 仰角 C = arg7 ± arg8
            const float theC = arg[7] + arg[8] * (Rand01() * 2.0f - 1.0f);

            // 方位 B: arg10 != 0 ならカウンタ/(arg10+1) で均等、そうでなければ乱数
            float theB;
            if (arg[10] > 0.5f)
                theB = 6.283185307179586f * (float)(mi.emitSpawned % ((int)arg[10] + 1)) / ((float)arg[10] + 1.0f);
            else
                theB = Rand01() * 6.283185307179586f;
            theB += phase;

            // 実測: v = ( arg2·cosC·cosB, arg3·sinC, arg4·cosC·sinB )  ← 軸ごとの半径
            const float cC = cosf(theC), sC = sinf(theC);
            const float cB = cosf(theB), sB = sinf(theB);
            float x1 = arg[2] * cC * cB;
            float y1 = arg[3] * sC;
            float z1 = arg[4] * cC * sB;

            // 実測: thunk_0042ed05 = RotZ(arg5)（row0 = cos,sin,0,0）、
            //       thunk_0042ec62 = RotY(arg6)（row0 = cos,0,-sin,0）の順に適用
            const float cz = cosf(arg[5]), sz5 = sinf(arg[5]);
            const float x2 = x1 * cz - y1 * sz5;
            const float y2 = x1 * sz5 + y1 * cz;
            const float z2 = z1;

            const float cy = cosf(arg[6]), sy6 = sinf(arg[6]);
            const float x3 =  x2 * cy + z2 * sy6;
            const float y3 =  y2;
            const float z3 = -x2 * sy6 + z2 * cy;

            float* pp = su.sub.Pos();
            pp[0] += x3;
            pp[1] += y3;
            pp[2] += z3;
            break;
        }

        //--- 乱数レンジ加算 --------------------------------------------
        // AltanaView FUN_0040e650 の確定仕様:
        //   0x0A : Move  += rand(-arg, +arg)   （対称・成分ごとに独立）
        //          実測: `(arg - (-arg)) * r + (-arg) + d` = arg*(2r-1) + d
        //   0x10 : Scale += rand(0, arg)       （成分ごとに独立）
        //   0x11 : Scale += rand(0, arg[0])    （3 成分に同じ値）
        // ※ 0x10/0x11 は AltanaView ではサブ再初期化（+0x5e）と併せて機能するため、
        //   再初期化を実装するまでは従来の近似を維持する。
        case 0x0A:
        {
            float* t = su.sub.Move();
            t[0] += arg[0] * (Rand01() * 2.0f - 1.0f);
            t[1] += arg[1] * (Rand01() * 2.0f - 1.0f);
            t[2] += arg[2] * (Rand01() * 2.0f - 1.0f);
            break;
        }
        case 0x10:
        {
            float* t = su.sub.Scale();
            t[0] += arg[0] * Rand01();
            t[1] += arg[1] * Rand01();
            t[2] += arg[2] * Rand01();
            break;
        }
        case 0x11:
        {
            float* t = su.sub.Scale();
            const float r = Rand01();
            t[0] += arg[0] * r; t[1] += arg[0] * r; t[2] += arg[0] * r;
            break;
        }

        //--- キーフレーム表参照（時間でレコード選択）-------------------
        case 0x4F:
        {
            uint32_t tbl = 0;
            memcpy(&tbl, p + 8, 4);
            if (mi.dat->InRange(tbl, 0x14))
            {
                uint32_t n = 0;
                memcpy(&n, mi.dat->At(tbl) + 0x10, 4);
                if (n != 0)
                {
                    const uint32_t idx = (ctx.Frame() % n + 2) * 0x10;
                    if (mi.dat->InRange(tbl + idx, 12))
                    {
                        const float* rec = reinterpret_cast<const float*>(mi.dat->At(tbl + idx));
                        float* t = su.sub.Pos();
                        t[0] += rec[0]; t[1] += rec[1]; t[2] += rec[2];
                    }
                }
            }
            break;
        }

        //--- 反転トレイル（実測 FUN_0040e650 op 0x67 / L11767-11778）
        //   pos = -dest * dur * 60      （dest = 出力スロット、dur = ctx+0x1c8、C50 = 60）
        case 0x67:
        {
            if (d)
            {
                const float k = ctx.Duration() * 60.0f;
                float* t = su.sub.Pos();
                t[0] = -d[0] * k;
                t[1] = -d[1] * k;
                t[2] = -d[2] * k;
            }
            break;
        }

        //--- 親ボーン合成（本ツールは親無し＝平行移動として近似）--------
        case 0x45:
        {
            float* t = su.sub.Pos();
            t[0] += arg[0]; t[1] += arg[1]; t[2] += arg[2];
            break;
        }

        //--- 出力スロットへの対称乱数（実測: LAB_0040e8cb）
        //   0x03 / 0x0C / 0x13 : dest[i] += rand(-arg[i], +arg[i])  （成分ごと独立）
        case 0x03:
        case 0x0C:
        case 0x13:
            if (d)
            {
                d[0] += arg[0] * (Rand01() * 2.0f - 1.0f);
                d[1] += arg[1] * (Rand01() * 2.0f - 1.0f);
                d[2] += arg[2] * (Rand01() * 2.0f - 1.0f);
            }
            break;

        //--- 未確定: 出力スロットへ加算で近似 ---------------------------
        case 0x08:
        case 0x41:
            if (d) { d[0] += arg[0]; d[1] += arg[1]; d[2] += arg[2]; }
            break;

        //--- Move の固定回転（実測: Cure 10/9 の prn0/prn1 が該当）-------
        //   prn1: Move=(-2.4783,0,0) に (0, 1.7453, 0)=100°、prn0 は 80°。
        //   回転はベクトル長を保つ（2.4783 のまま）ので、生成ごとの向きだけが
        //   変わる = 星が扇状に散る。適用順は 0x1F で確定した Z→Y に合わせ、
        //   X→Y→Z の順で 1 回ずつ。
        case 0x3B:
        {
            float* mv = su.sub.Move();
            float x = mv[0], y = mv[1], z = mv[2];
            if (arg[0] != 0.0f)                       // X
            {
                const float c = cosf(arg[0]), s = sinf(arg[0]);
                const float y2 = y * c - z * s;
                const float z2 = y * s + z * c;
                y = y2; z = z2;
            }
            if (arg[1] != 0.0f)                       // Y
            {
                const float c = cosf(arg[1]), s = sinf(arg[1]);
                const float x2 =  x * c + z * s;
                const float z2 = -x * s + z * c;
                x = x2; z = z2;
            }
            if (arg[2] != 0.0f)                       // Z
            {
                const float c = cosf(arg[2]), s = sinf(arg[2]);
                const float x2 = x * c - y * s;
                const float y2 = x * s + y * c;
                x = x2; y = y2;
            }
            mv[0] = x; mv[1] = y; mv[2] = z;
            break;
        }

        //--- 面内回転（スピン）-----------------------------------------
        //   Cure 10/9 の kr10 にだけ 0x3D → 0x3E → 0x40 が並び、同じ kr10 の
        //   VM#2 にだけ 0x29/0x2B が現れる（kr11/kr12 は両方なし）。
        //   0x3D は引数なし（len=1）= 角度の 0 初期化、0x3E/0x40 は同じ
        //   (0.04, 0.02) を持つ = 「初期角の乱数範囲」と「角速度の乱数範囲」。
        //   意味が確定するまでは --spin on のときだけ作用させる。
        case 0x3D:
            if (g_Spin)
            {
                float* sp = su.sub.Spin();
                float* sv = su.sub.SpinV();
                sp[0] = sp[1] = sp[2] = 0.0f;
                sv[0] = sv[1] = sv[2] = 0.0f;
            }
            break;
        case 0x3E:                                    // 初期角（X, Z）
            if (g_Spin)
            {
                float* sp = su.sub.Spin();
                sp[0] += (Rand01() * 2.0f - 1.0f) * arg[0];
                sp[2] += (Rand01() * 2.0f - 1.0f) * arg[1];
            }
            break;
        case 0x40:                                    // 角速度（X, Z）
            if (g_Spin)
            {
                float* sv = su.sub.SpinV();
                sv[0] += (Rand01() * 2.0f - 1.0f) * arg[0];
                sv[2] += (Rand01() * 2.0f - 1.0f) * arg[1];
            }
            break;

        //--- Move の散布（意味未確定・--scatter on のときだけ）-----------
        //   Cure 10/9 の prn0/prn1 が同一の 9 引数を持つ:
        //     (1, 3.8, 6.4, 0, 0, -0, 0, FLT_MAX, 0)
        //   3.8 / 6.4 rad は角度レンジ（217°..367°）と読めるため、Move を
        //   その範囲の乱数角で Y 回転する。arg7 = FLT_MAX は「クランプ無し」の
        //   歩哨とみなし、座標には一切足さない（発散の防止）。
        case 0x3A:
        {
            if (!g_Scatter) break;
            float* mv = su.sub.Move();
            const float a0 = arg[1], a1 = arg[2];
            if (a1 <= a0) break;
            const float th = a0 + (a1 - a0) * Rand01();
            const float c = cosf(th), s = sinf(th);
            const float x =  mv[0] * c + mv[2] * s;
            const float z = -mv[0] * s + mv[2] * c;
            mv[0] = x; mv[2] = z;
            break;
        }

        default:
            break;                                // 未実装 op は無視
        }

        const uint32_t len = InstLen(p);
        if (len == 0) break;
        ip += len * 4;
    }
}

//======================================================================
//  VM#2 : 時間積分（FUN_0040f4b0）
//  dur は「ルート（エフェクト全体）の継続時間」= EffectRoot+0x1c8。
//  ジェネレータ個別の長さ（+0x76）ではない点に注意。
//======================================================================
static bool Vm2(const MotionInstance& mi, SubUnit& su, float dt, float rootDuration,
                const EffectContext& ctx)
{
    float dur = rootDuration;
    // --life on: ジェネレータの LifeTime（op 0x01 の +0x22、60 = 1 秒）を寿命に使う
    if (mi.gen != nullptr && mi.gen->lifeFrames > 0 && ctx.LifeFromData())
        dur = (float)mi.gen->lifeFrames / 60.0f;
    if (dur <= 0.0f) dur = (mi.gen->Length() > 0.0f) ? mi.gen->Length() : (1.0f / 60.0f);

    //--------------------------------------------------------------
    // 経過時間の更新
    //   AltanaView FUN_0040f4b0: 総継続時間に達したら t01=1・elapsed は dur で頭打ち
    //   （折返しはしない。寿命後の扱いは呼び出し側 = 再初期化 or 削除）
    //--------------------------------------------------------------
    float t = su.sub.Elapsed() + dt;
    const float tOld = su.sub.T01();          // 補間の基準（元実装 particle[0x3c] の旧値）
    bool ended = false;
    if (dur > 0.0f && t >= dur)
    {
        t = dur;
        ended = true;
    }
    su.sub.Elapsed() = t;
    su.sub.T01()     = (dur > 0.0f) ? (t / dur) : 0.0f;

    //--------------------------------------------------------------
    // 前フレームとの差分（速度）を保存  [0x3d..0x3f]
    //--------------------------------------------------------------
    float* prev = su.sub.At(SUB_PREV);
    const float* cur = su.sub.Pos();
    prev[0] = cur[0]; prev[1] = cur[1]; prev[2] = cur[2];

    //--------------------------------------------------------------
    // VM#2 プログラム（tblC）: dt を掛けて積分する
    //--------------------------------------------------------------
    const float k = dt * 60.0f;
    uint32_t ip = mi.gen->tblC;

    for (int guard = 0; guard < 8192; ++guard)
    {
        const uint8_t* p = mi.dat->At(ip);
        if (p == nullptr || !mi.dat->InRange(ip, 8)) break;

        uint32_t w0 = 0;
        memcpy(&w0, p, 4);
        const uint8_t op = (uint8_t)(w0 & 0xFF);
        if (op == 0) break;

        ++g_OpStat[2][op];                        // --opstat 用

        const float* arg = reinterpret_cast<const float*>(p + 4);
        const uint32_t dest = w0 >> 13;
        // 問題 5: Vm1 と同じ境界判定（dest + 4 <= SUB_DEST_N）
        float* d = (dest + 4u <= SUB_DEST_N) ? su.sub.Dest(dest) : nullptr;

        switch (op)
        {
        // op 別アキュムレータ（AltanaView では 0x78+op*4 ＝ pos/scale と重なる）
        // ※ 引数（arg）を積分する案も試したが、g006 が 200 m 超に飛ぶなど破綻したため
        //    dest スロット（d）を積分する従来動作を維持する。
        case 0x02:
        case 0x05:
        case 0x08:
            if (d)
            {
                float* acc = su.sub.At(0x78 + (uint32_t)op * 4);
                acc[0] += k * d[0];
                acc[1] += k * d[1];
                acc[2] += k * d[2];
            }
            break;

        case 0x03:
        case 0x06:
        case 0x09:
            if (d)
            {
                d[0] += k * arg[0];
                d[1] += k * arg[1];
                d[2] += k * arg[2];
            }
            break;

        case 0x2C:      // 指数的レート
            if (d && arg[0] > 0.0f)
            {
                const float r = powf(arg[0], k);
                d[0] *= r; d[1] *= r; d[2] *= r;
            }
            break;

        //--- キーフレーム補間（実測 FUN_0040f4b0 L12136: 0x0E < op < 0x22）
        //   出力先は **op で決まる固定スロット** = (op + 0x11) * 4 バイト
        //     0x0F..0x11 → Pos(0x80) / 0x12..0x14 → Move(0x8C, ×π) /
        //     0x15..0x17 → Scale(0x98) / 0x18..0x1B → Color(0xA4) /
        //     0x1C..0x21 → UV(0xB4..)
        //   キー表は VM#1 が dest スロットに設定した 0x19 チャンク参照
        //     （オフセットではなく名前。VM#1 側で解決済み。問題 1）
        case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13: case 0x14:
        case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: case 0x1A:
        case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F:
        case 0x20: case 0x21:
        {
            const uint32_t first = (dest < SUB_DEST_N) ? su.keyFirst[dest] : 0u;
            const uint32_t count = (dest < SUB_DEST_N) ? (uint32_t)su.keyCount[dest] : 0u;
            // 未解決（0x19 名が見つからない）ときは**書かない**。
            //   以前は 0 で上書きし、Scale の縮退と α=0 を招いていた。
            if (first != 0u && count != 0u)
            {
                // 0x12..0x14 は角度（度 → ラジアン）
                const float scale = (op >= 0x12 && op <= 0x14) ? 3.14159265f : 1.0f;
                float* outSlot = su.sub.At(((uint32_t)op + 0x11u) * 4u);
                if (g_KeyIncr)
                {
                    // 実測 FUN_0040f820: カーソル前進＋現在値基準の増分補間
                    uint32_t cur = (dest < SUB_DEST_N) ? su.keyCur[dest] : 0u;
                    outSlot[0] = EvalKeyTrackIncr(*mi.dat, first, count, tOld, su.sub.T01(),
                                                  scale, cur, outSlot[0]);
                    if (dest < SUB_DEST_N) su.keyCur[dest] = (uint8_t)cur;
                }
                else
                {
                    outSlot[0] = EvalKeyTrack(*mi.dat, first, count, su.sub.T01(), scale);
                }
            }
            break;
        }

        // UV オフセット（AltanaView: `puVar9[op + 6] += k * arg[0]`）
        //   op 0x27 → スロット 0x2D (=0xB4:u) / op 0x28 → 0x2E (=0xB8:v)
        //   累積値は 0..1 を巡回させる（描画側で frac して適用）
        case 0x27:
        case 0x28:
            {
                float* acc = su.sub.At(((uint32_t)op + 6) * 4);
                acc[0] += k * arg[0];
            }
            break;

        //--- 面内回転の積分 ---------------------------------------------
        //   0x29 / 0x2B は 0x27/0x28 と同じ「op ごとの固定スロット」型。
        //   Cure 10/9 の kr10 だけが持ち、引数は ±0.1222 rad（≈7°/frame）。
        //   0x29 = 角度、0x2B = 角速度の積分として扱う（--spin on のみ）。
        case 0x29:
            if (g_Spin)
            {
                float* sp = su.sub.Spin();
                sp[0] += k * arg[0];
                sp[1] += k * arg[1];
                sp[2] += k * arg[2];
            }
            break;
        case 0x2B:
            if (g_Spin)
            {
                float* sv = su.sub.SpinV();
                sv[0] += k * arg[0];
                sv[1] += k * arg[1];
                sv[2] += k * arg[2];
            }
            break;

        default:
            break;
        }

        const uint32_t len = InstLen(p);
        if (len == 0) break;
        ip += len * 4;
    }

    //--------------------------------------------------------------
    // 寿命: ジェネレータの長さを 1 周したか（呼び出し側がインスタンスを終了）
    //--------------------------------------------------------------
    return ended;
}

//======================================================================
//  MotionInstance::SpawnSub / RespawnSub
//----------------------------------------------------------------------
//  AltanaView の粒子生成（FUN_0040e0b0 の放出部）と再初期化（FUN_00410090）
//    - 生成時: VM#1（FUN_0040e650）を 1 回だけ実行して初期状態を作る
//    - 寿命時: elapsed/T01 を 0 に戻し、Move に乱数位相・UV に乱数オフセットを
//              加え（符号はマイナス）、端数時間だけ進める
//======================================================================
void MotionInstance::SpawnSub(const EffectContext& ctx, bool applyGeo)
{
    subs.push_back(SubUnit{});
    InitSub(subs.back(), applyGeo);
    Vm1(*this, subs.back(), ctx);
    ++emitSpawned;
}

void MotionInstance::RespawnSub(float carryDt)
{
    if (subs.empty()) return;
    SubUnit& su = subs.back();

    su.sub.Elapsed() = 0.0f;
    su.sub.T01()     = 0.0f;

    const float kTwoPi = 6.28318530717959f;
    float* mv = su.sub.Move();
    mv[0] -= Rand01() * kTwoPi;
    mv[1] -= Rand01() * kTwoPi;
    mv[2] -= Rand01() * kTwoPi;

    float* uv0 = su.sub.At(0xB4);      // op 0x27 の u オフセット
    float* uv1 = su.sub.At(0xB8);      // op 0x28 の v オフセット
    uv0[0] -= Rand01();
    uv1[0] -= Rand01();

    su.sub.Elapsed() = (carryDt > 0.0f && carryDt < 1.0f) ? carryDt : 0.0f;
}

//======================================================================
//  MotionInstance::Eval
//======================================================================
void MotionInstance::Eval(const EffectContext& ctx, float dt)
{
    if (gen == nullptr || dat == nullptr) return;

    bool ended = false;

    const bool emitMode = ctx.Emit();

    //--------------------------------------------------------------
    // 連続放出（AltanaView FUN_0040e0b0）
    //   寿命のあいだに emitTotal 個を等間隔で 1 個ずつ生成する
    //--------------------------------------------------------------
    if (emitMode)
    {
        const float dur = (ctx.Duration() > 0.0f) ? ctx.Duration()
                        : ((gen->Length() > 0.0f) ? gen->Length() : (1.0f / 60.0f));
        if (emitInterval <= 0.0f)
        {
            // データの +0x78 は 0 のことが多い（実測: 11\18 の全 generator）ため、
            // 粒子数が 1 のときはレート（既定 24 個/秒）で放出する。
            //   → 寿命のあいだ粒子が積み上がり、参照のような「柱」になる
            const float rate = (emitTotal > 1) ? ((float)emitTotal / dur) : (float)g_EmitRate;
            emitInterval = 1.0f / ((rate > 0.0f) ? rate : 1.0f);
            if (emitInterval <= 0.0f) emitInterval = 1.0f / 60.0f;
        }

        {
            emitTimer -= dt;
            int guard = 0;
            while (emitTimer <= 0.0f &&
                   (float)emitSpawned * emitInterval < dur &&
                   subs.size() < 512u && guard++ < 256)
            {
                SpawnSub(ctx, ctx.Geo());
                emitTimer += emitInterval;
            }
        }
    }

    // AltanaView と同じく、**サブ（粒子）ごと**に評価する
    for (size_t k = 0; k < subs.size(); )
    {
        SubUnit& su = subs[k];

        // 従来モードは VM#1 を毎フレーム回す（旧挙動の再現）。
        // 放出モードでは生成時に 1 回だけ回している。
        if (!emitMode) Vm1(*this, su, ctx);

        const bool subEnded = Vm2(*this, su, dt, ctx.Duration(), ctx);
        if (subEnded)
        {
            ended = true;
            if (emitMode)
            {
                if (ctx.LoopRespawn())
                {
                    RespawnSub(dt);              // ループ: 再初期化して使い回す
                }
                else
                {
                    subs.erase(subs.begin() + (ptrdiff_t)k);
                    continue;                    // 寿命: 削除
                }
            }
        }
        else if (g_MoveInt)
        {
            //----------------------------------------------------------
            // Move(0x8C) = 速度 [m/s] を位置へ積分する（--moveint）
            //   VM#1 0x09 が Move に速度ベクトルを書き、VM#2 0x05 が積む。
            //   描画側は Pos しか読んでいなかったため Move が捨てられていた。
            //   例: Cure 10/9 の prn1 は Move=(-2.4783,0,0) → 0x3B で 100° 回転
            //       → 2.4783 m/s × 寿命 0.167 s = 0.41 m の位置に星が出る。
            //----------------------------------------------------------
            float* pp = su.sub.Pos();
            const float* mv = su.sub.Move();
            pp[0] += mv[0] * dt;
            pp[1] += mv[1] * dt;
            pp[2] += mv[2] * dt;
        }

        //----------------------------------------------------------
        // 0x21 のフレーム（テクスチャアトラス）
        //----------------------------------------------------------
        if (gen->HasFrames() && ctx.UseTexFrames())
        {
            const int n = (int)gen->texFrames.size();
            int idx;
            if (ctx.RandomFrame())
            {
                // AltanaView: 描画ごとに 0x21 のフレームを乱数で 1 枚選ぶ（FUN_0040c4e0）
                idx = (int)(Rand01() * (float)n);
                if (idx >= n) idx = n - 1;
            }
            else
            {
                idx = (int)(su.sub.Elapsed() * 60.0f);
                if (idx < 0) idx = 0;
                idx %= n;
            }

            su.frameIdx = idx;
            const TexFrame& f = gen->texFrames[idx];
            su.uv[0] = f.u0; su.uv[1] = f.v0; su.uv[2] = f.u1; su.uv[3] = f.v1;
            su.quadHalf[0]   = (f.x1 - f.x0) * 0.5f;
            su.quadHalf[1]   = (f.y1 - f.y0) * 0.5f;
            su.quadCenter[0] = (f.x1 + f.x0) * 0.5f;
            su.quadCenter[1] = (f.y1 + f.y0) * 0.5f;
        }

        ++k;
    }

    // 寿命: 従来モード（--emit off）はインスタンス単位で再初期化／終了する。
    //       放出モードでは粒子ごとに Eval 内で処理済み（FUN_0040e0b0 と同じ）。
    if (ended && !emitMode)
    {
        if (ctx.LoopRespawn())
        {
            for (SubUnit& su : subs)
            {
                su.sub.Elapsed() = 0.0f;
                su.sub.T01()     = 0.0f;

                // Move（0x8C..）を 0..255 の乱数 * 1/60 [m] だけずらす
                float* mv = su.sub.Move();
                mv[0] -= (float)(RandByte()) * (1.0f / 60.0f);
                mv[1] -= (float)(RandByte()) * (1.0f / 60.0f);
                mv[2] -= (float)(RandByte()) * (1.0f / 60.0f);

                // UV オフセット（0xB4/0xB8）も同様に
                float* uvo = su.sub.At(0xB4);
                uvo[0] -= (float)(RandByte());
                uvo[1] -= (float)(RandByte());
            }
            expired = false;
        }
        else
        {
            expired = true;
        }
    }
}

//======================================================================
//  VM#1 のキー表参照（0x19 名）の解決監査（--selftest）
//    VM#1 を実行せずにプログラムだけを走査し、
//      ops        = キー op（0x21..0x2F / 0x33..0x36 / 0x59..0x5B）の総数
//      resolved   = 第 3 dword が 0x19 チャンク名として解決できた数
//    「解決率 100%」をゴールデンにするための観測点（問題 1 の検証）。
//======================================================================
KeyAudit EffectKeyAudit(const DatFile& dat, const std::vector<GeneratorBlock>& gens)
{
    KeyAudit out;
    for (const GeneratorBlock& g : gens)
    {
        uint32_t ip = g.tblB;
        for (int guard = 0; guard < 8192; ++guard)
        {
            const uint8_t* p = dat.At(ip);
            if (p == nullptr || !dat.InRange(ip, 8)) break;

            uint32_t w0 = 0;
            memcpy(&w0, p, 4);
            const uint8_t op = (uint8_t)(w0 & 0xFF);
            if (op == 0) break;                       // 終端
            const uint32_t len = (uint32_t)(p[1] & 0x1F);

            if (IsKeyOp(op))
            {
                ++out.ops;
                char nm[5] = {};
                if (dat.InRange(ip + 8, 4)) memcpy(nm, p + 8, 4);
                const DatChunk* kc = dat.Find(nm, 0x19);
                if (kc != nullptr && kc->size > 0x18) ++out.resolved;
            }

            if (len == 0) break;
            ip += len * 4;
        }
    }
    out.unresolved = out.ops - out.resolved;
    return out;
}

