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

namespace
{
    inline float Rand01() { return (float)rand() / (float)RAND_MAX; }
    inline uint32_t InstLen(const uint8_t* p) { return (uint32_t)(p[1] & 0x1F); }
}

//======================================================================
//  VM#1 : 目標値評価（FUN_0040e650）
//======================================================================
static void Vm1(MotionInstance& mi, const EffectContext& ctx)
{
    uint32_t ip = mi.gen->tblB;

    for (int guard = 0; guard < 8192; ++guard)
    {
        const uint8_t* p = mi.dat->At(ip);
        if (p == nullptr || !mi.dat->InRange(ip, 8)) break;

        uint32_t w0 = 0;
        memcpy(&w0, p, 4);
        const uint8_t op = (uint8_t)(w0 & 0xFF);
        if (op == 0) break;                       // 終端

        const float* arg = reinterpret_cast<const float*>(p + 4);
        const uint32_t dest = w0 >> 13;
        float* d = (dest < SUB_DEST_N) ? mi.sub.Dest(dest) : nullptr;

        switch (op)
        {
        //--- 定数 float3 → 出力スロット ---------------------------------
        case 0x02:
        case 0x0B:
        case 0x12:
            if (d) { d[0] = arg[0]; d[1] = arg[1]; d[2] = arg[2]; }
            break;

        //--- 定数 float3 → MOVE スロット --------------------------------
        case 0x09:
        case 0x0F:
        {
            float* t = mi.sub.Move();
            t[0] = arg[0]; t[1] = arg[1]; t[2] = arg[2];
            break;
        }

        //--- 位置に加算 / スケールに乗算 --------------------------------
        case 0x47:
        {
            float* t = mi.sub.Pos();
            t[0] += arg[0]; t[1] += arg[1]; t[2] += arg[2];
            break;
        }
        case 0x49:
        {
            float* t = mi.sub.Scale();
            t[0] *= arg[0]; t[1] *= arg[1]; t[2] *= arg[2];
            break;
        }

        //--- RGBA（4 バイト）→ 色 --------------------------------------
        case 0x16:
        {
            const uint8_t* b = p + 4;
            float* c = mi.sub.Color();
            c[0] = b[0] / 255.0f;
            c[1] = b[1] / 255.0f;
            c[2] = b[2] / 255.0f;
            c[3] = b[3] / 255.0f;
            break;
        }

        //--- 乱数レンジ加算 --------------------------------------------
        case 0x0A:
        case 0x10:
        case 0x11:
        {
            float* t = (op == 0x11) ? mi.sub.Scale() : mi.sub.Move();
            const float r = Rand01();
            t[0] += arg[0] * r; t[1] += arg[1] * r; t[2] += arg[2] * r;
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
                        float* t = mi.sub.Pos();
                        t[0] += rec[0]; t[1] += rec[1]; t[2] += rec[2];
                    }
                }
            }
            break;
        }

        //--- 反転トレイル（位置 = -位置 × 係数）------------------------
        case 0x67:
        {
            float* t = mi.sub.Pos();
            const float k = mi.sub.At(SUB_SCALE)[0];
            t[0] = -t[0] * k;
            t[1] = -t[1] * k;
            t[2] = -t[2] * k;
            break;
        }

        //--- 親ボーン合成（本ツールは親無し＝平行移動として近似）--------
        case 0x45:
        {
            float* t = mi.sub.Pos();
            t[0] += arg[0]; t[1] += arg[1]; t[2] += arg[2];
            break;
        }

        //--- 未確定（回転・補間系）: 出力スロットへ加算で近似 -----------
        case 0x03:
        case 0x08:
        case 0x0C:
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
static void Vm2(MotionInstance& mi, float dt, float rootDuration)
{
    float dur = rootDuration;
    if (dur <= 0.0f) dur = (mi.gen->Length() > 0.0f) ? mi.gen->Length() : (1.0f / 60.0f);

    //--------------------------------------------------------------
    // 経過時間の更新（AltanaView と同じくループ折返し）
    //--------------------------------------------------------------
    float t = mi.sub.Elapsed() + dt;
    bool wrapped = false;
    if (t >= dur)
    {
        t -= dur * floorf(t / dur);
        if (t < 0.0f) t = 0.0f;
        wrapped = true;
    }
    mi.sub.Elapsed() = t;
    mi.sub.T01()     = (dur > 0.0f) ? (t / dur) : 0.0f;

    //--------------------------------------------------------------
    // 前フレームとの差分（速度）を保存  [0x3d..0x3f]
    //--------------------------------------------------------------
    float* prev = mi.sub.At(SUB_PREV);
    const float* cur = mi.sub.Pos();
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

        const float* arg = reinterpret_cast<const float*>(p + 4);
        const uint32_t dest = w0 >> 13;
        float* d = (dest < SUB_DEST_N) ? mi.sub.Dest(dest) : nullptr;

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
                float* acc = mi.sub.At(0x78 + (uint32_t)op * 4);
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

        default:
            break;
        }

        const uint32_t len = InstLen(p);
        if (len == 0) break;
        ip += len * 4;
    }

    //--------------------------------------------------------------
    // 寿命: ジェネレータの長さを 1 周したら終了
    //--------------------------------------------------------------
    if (wrapped) mi.expired = true;
}

//======================================================================
//  MotionInstance::Eval
//======================================================================
void MotionInstance::Eval(const EffectContext& ctx, float dt)
{
    if (gen == nullptr || dat == nullptr) return;

    Vm1(*this, ctx);
    Vm2(*this, dt, ctx.Duration());

    //--------------------------------------------------------------
    // 0x21 のフレーム（テクスチャアトラス）: 60 fps で進め、末尾でループ
    //--------------------------------------------------------------
    if (gen->HasFrames() && ctx.UseTexFrames())
    {
        const int n = (int)gen->texFrames.size();
        int idx = (int)(sub.Elapsed() * 60.0f);
        if (idx < 0) idx = 0;
        idx %= n;

        frameIdx = idx;
        const TexFrame& f = gen->texFrames[idx];
        uv[0] = f.u0; uv[1] = f.v0; uv[2] = f.u1; uv[3] = f.v1;
        quadHalf[0]   = (f.x1 - f.x0) * 0.5f;
        quadHalf[1]   = (f.y1 - f.y0) * 0.5f;
        quadCenter[0] = (f.x1 + f.x0) * 0.5f;
        quadCenter[1] = (f.y1 + f.y0) * 0.5f;
    }
}

