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
    //   カーソル（粒子ごと）を前進させ、**現在値 `out` を基準に**増分で寄せる:
    //     out += (tNew - tOld) * (scale*value - out) / (keyTime - tOld)
    //   → 絶対補間ではなく「キー時刻までの残り距離で按分する」形。
    //   tOld == tNew のときは何もしない（= 元実装の早期 return）。
    float EvalKeyTrackIncr(const DatFile& dat, uint32_t first, uint32_t count,
                           float tOld, float tNew, float scale,
                           uint32_t& cursor, float out)
    {
        if (first == 0u || count == 0u) return out;
        if (tOld == tNew) return out;
        if (count > 64u) count = 64u;
        if (cursor >= count) cursor = count - 1;

        float kt = 0.0f, kv = 0.0f;
        // カーソルを tNew 以下の最後のキーまで前進（元実装はポインタを進めるだけ）
        while (cursor + 1u < count)
        {
            const uint32_t en = first + (cursor + 1u) * 8u;
            if (!dat.InRange(en, 8)) break;
            const uint8_t* qn = dat.At(en);
            float tn = 0.0f, vn = 0.0f;
            memcpy(&tn, qn, 4); memcpy(&vn, qn + 4, 4);
            if (tNew < tn) break;
            ++cursor;
        }
        const uint32_t ec = first + cursor * 8u;
        if (!dat.InRange(ec, 8)) return out;
        const uint8_t* q = dat.At(ec);
        memcpy(&kt, q, 4);
        memcpy(&kv, q + 4, 4);
        if (kt < -1e6f || kt > 1e6f || kv < -1e12f || kv > 1e12f) return out;

        const float keyVal = scale * kv;
        const float denom  = kt - tOld;
        if (fabsf(denom) < 1e-6f) return keyVal;
        return out + (tNew - tOld) * (keyVal - out) / denom;
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
    bool g_KeyIncr  = false;       // キー補間を実測どおり「カーソル前進＋増分」で行う（--keymode incr）
                                   //   ※ 逆コンパイルの解釈が未確定のため既定 off（abs）。incr は 11\18 で
                                   //     hollow 2.04 -> 3.40 と悪化した（アセンブリで要確認）
    bool g_RingDist = false;       // 0x1F の半径に arg7（-0.4..-0.8 の絶対値）を使う
                                   //   （--ringradius dist。既定 xy = arg0/arg1）
    float g_RingScale = 4.0f;      // 円周半径の倍率（--ringscale N。既定 4）
                                   //   引数の半径 (0.1..0.5 m) は「サブ粒子スケール」で、
                                   //   ビルボード（2..4 m）より小さいため、そのままでは
                                   //   円周が埋もれて直線に見える。4 倍でリングとして見える。
    int   g_Arrange  = 1;          // 0=line（旧）/ 1=ring（円周・均等）/ 2=spiral（らせん）
    int   g_Plane    = 0;          // 0=xy / 1=xz / 2=yz（円周を張る面）
    float g_SpiralTurns = 1.0f;    // らせんの巻き数（--spiralturns）
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
    //   0x45/0x47/0x49 以外の op は実行されない。当方では位置は骨行列で与えるため、
    //   VM#1 自体を省略する（Pos は 0 のまま → 骨の位置に描かれる）。
    if (ctx.Attached()) return;

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

            // 位相（dest 値はスカラー倍率）
            const float phase = (d != nullptr) ? (d[0] * (1.0f / 65536.0f)) : 0.0f;

            // 2 番目の角度: [arg7-arg8, arg7+arg8]
            const float theta = arg[7] + arg[8] * (Rand01() * 2.0f - 1.0f);

            // 方位: arg10 > 0 なら粒子番号で均等、そうでなければ乱数
            float phi;
            if (arg[9] > 0.5f && mi.emitTotal > 1)
                phi = 6.283185307179586f * (float)(mi.emitSpawned % mi.emitTotal) / (float)mi.emitTotal;
            else
                phi = Rand01() * 6.283185307179586f;
            phi += phase;

            // Y 回転 → X 回転（thunk の順序に合わせる。要実測確認）
            const float cp = cosf(phi), sp2 = sinf(phi);
            const float rx =  vx0 * cp + vz0 * sp2;
            const float rz = -vx0 * sp2 + vz0 * cp;
            const float ct = cosf(theta), st = sinf(theta);
            const float ry =  vy0 * ct - rz * st;
            const float rz2 = vy0 * st + rz * ct;

            float* pp = su.sub.Pos();
            pp[0] += rx;
            pp[1] += ry;
            pp[2] += rz2;
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

        if (Vm2(*this, su, dt, ctx.Duration(), ctx))
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

