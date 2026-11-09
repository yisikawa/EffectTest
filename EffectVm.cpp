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
    // 0..255 の乱数（AltanaView FUN_0044c55c。色バイトやジッタ量に使われる）
    inline int RandByte() { return (int)(Rand01() * 256.0f) & 0xFF; }
    inline uint32_t InstLen(const uint8_t* p) { return (uint32_t)(p[1] & 0x1F); }
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

        const float* arg = reinterpret_cast<const float*>(p + 4);
        const uint32_t dest = w0 >> 13;
        float* d = (dest < SUB_DEST_N) ? su.sub.Dest(dest) : nullptr;

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
            float* t = su.sub.Move();
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

        //--- 乱数レンジ加算 --------------------------------------------
        // AltanaView FUN_0040e650 の確定仕様:
        //   0x0A : Move  += rand(-arg, +arg)   （対称）
        //   0x10 : Scale += rand(0, arg)       （成分ごと）
        //   0x11 : Scale += rand(0, arg[0])    （3 成分に同じ値）
        // ※ そのまま適用するとスケールが単調増加し、参照の約 2 倍に破綻した
        //   （AltanaView は寿命でサブを再初期化する経路 `+0x5e` と併せて機能する）。
        //   再初期化を実装するまで従来の近似を維持する。
        case 0x0A:
        case 0x10:
        case 0x11:
        {
            float* t = (op == 0x11) ? su.sub.Scale() : su.sub.Move();
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
                        float* t = su.sub.Pos();
                        t[0] += rec[0]; t[1] += rec[1]; t[2] += rec[2];
                    }
                }
            }
            break;
        }

        //--- 反転トレイル（位置 = -位置 × 係数）------------------------
        case 0x67:
        {
            float* t = su.sub.Pos();
            const float k = su.sub.At(SUB_SCALE)[0];
            t[0] = -t[0] * k;
            t[1] = -t[1] * k;
            t[2] = -t[2] * k;
            break;
        }

        //--- 親ボーン合成（本ツールは親無し＝平行移動として近似）--------
        case 0x45:
        {
            float* t = su.sub.Pos();
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

        const float* arg = reinterpret_cast<const float*>(p + 4);
        const uint32_t dest = w0 >> 13;
        float* d = (dest < SUB_DEST_N) ? su.sub.Dest(dest) : nullptr;

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
//  MotionInstance::Eval
//======================================================================
void MotionInstance::Eval(const EffectContext& ctx, float dt)
{
    if (gen == nullptr || dat == nullptr) return;

    bool ended = false;

    // AltanaView と同じく、**サブ（粒子）ごと**に VM#1 → VM#2 を回す
    for (SubUnit& su : subs)
    {
        Vm1(*this, su, ctx);
        if (Vm2(*this, su, dt, ctx.Duration(), ctx)) ended = true;

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
    }

    // 寿命: 総継続時間に達したら、ループフラグがあればサブを再初期化し、
    //       無ければインスタンスを終了する（AltanaView FUN_0040e0b0 / FUN_00410090）
    if (ended)
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

