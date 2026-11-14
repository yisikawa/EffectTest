//======================================================================
//  EffectFfxi.cpp : ffxitool5.exe（D:\tools\ffxitool13032900）の
//  パーティクル実行パイプラインの移植（--ffxivm on で有効）
//
//  移植元（Ghidra 12.1.4 headless 逆コンパイル / docs/ANALYSIS-ffxitool5-Particle-2026-11-14.md）:
//    FUN_004041b0 : ジェネレータのデータ2/3 パース（type 索引 + 0x19 キー表、開始 = chunk+0x10）
//    FUN_00404750 : 放出（emitAge % (genFrames+1) == 0 で count+1 個）+ 粒子初期化
//    FUN_00403880 : 積分（データ3 type 2/5/8/0x27/0x28 をゲートに pos/rot/scl/w/h を進める）
//    FUN_00403940 : 0x19 キー表の {t,v} 線形補間（t==1.0 のペアは実キー）
//    FUN_004050b0 : 描画同期（t01 = age/(life+1)。キー 0x21..0x2F で pos/rot/scl/色/幅高さを上書き）
//
//  既知の近似（x87 スタックが原因で逆コンパイルが欠落した箇所）:
//    ・type 7（球面散布）の距離係数 unaff_EDI → 定数 EffectSphDist()（既定 1.0）
//    ・type 6 / type 7 の方向 = YPR(乱数3角) 行列 × (1,0,0) を正規化した単位球面乱数
//      （FUN_004119ca = YPR 行列、FUN_0041061d = Vec3TransformCoord、FUN_0041039e = Vec3Normalize
//       を D3DX 内部実装テーブル（FUN_00413c68 の退避テーブル）から同定済み）
//    ・type 6 はレコードの引数を読まない（ffxitool5 も読まない。単位方向をそのまま加算）
//    ・回転の rotY / rotZ（キー 0x25/0x26）はビルボードに写像しない（rotX = 0x24 がロール）
//======================================================================
#include "Effect.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace
{
    const float kTwoPi = 6.28318530717959f;

    //------------------------------------------------------------------
    // YPR(ψ,θ,φ) 行列 × (1,0,0) を正規化した方向（FUN_004119ca → FUN_0041061d の近似）。
    //   乱数は 3 回消費する（呼び出し側の RNG ストリーム整合のため）。
    //------------------------------------------------------------------
    void RandDir(float d[3])
    {
        const float y = EffectRand01() * kTwoPi;   // yaw
        const float p = EffectRand01() * kTwoPi;   // pitch
        const float r = EffectRand01() * kTwoPi;   // roll
        const float cy = cosf(y), sy = sinf(y);
        const float cp = cosf(p), sp = sinf(p);
        const float cr = cosf(r), sr = sinf(r);
        // M = Ry(y) × Rx(p) × Rz(r) × (1,0,0)
        d[0] = cy * cr - sy * sr * sp;
        d[1] = sr * cp;
        d[2] = sy * cr + cy * sr * sp;
        const float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (len > 1e-8f)
        {
            d[0] /= len; d[1] /= len; d[2] /= len;
        }
    }

    //------------------------------------------------------------------
    // FUN_00403940 の移植: {t,v} ペア列の線形補間。
    //   ・t==1.0 のペアは実キー（終端値を持つ）。ループは t==1.0 で抜けてその値を返す
    //   ・q（= t01）は 0..1 にクランプ
    //------------------------------------------------------------------
    float EvalFfxiKey(const DatFile& dat, const GeneratorBlock::FfxiKey& k, float q)
    {
        if (k.count == 0 || k.first == 0) return 0.0f;
        if (q < 0.0f) q = 0.0f;
        if (q > 1.0f) q = 1.0f;

        // ペア数はチャンクの実サイズにクランプする
        uint32_t n = k.count;
        uint32_t avail = 0;
        if (dat.InRange(k.first, n * 8))
            avail = n;
        else
        {
            // チャンク末尾を越えない範囲だけ読む
            const DatChunk* c = nullptr;
            for (const DatChunk& ch : dat.Chunks())
                if (ch.offset <= k.first && k.first < ch.offset + ch.size) { c = &ch; break; }
            if (c == nullptr) return 0.0f;
            avail = (c->offset + c->size - k.first) / 8;
            if (avail > n) avail = n;
        }
        if (avail == 0) return 0.0f;

        for (uint32_t i = 0; i < avail; ++i)
        {
            const uint8_t* p = dat.At(k.first + i * 8);
            if (p == nullptr) break;
            float t = 0.0f, v = 0.0f;
            memcpy(&t, p, 4);
            memcpy(&v, p + 4, 4);
            if (t == 1.0f) return v;                     // 終端（実キー）
            if (q == t) return v;
            if (i + 1 < avail)
            {
                const uint8_t* p2 = dat.At(k.first + (i + 1) * 8);
                if (p2 == nullptr) break;
                float t2 = 0.0f, v2 = 0.0f;
                memcpy(&t2, p2, 4);
                memcpy(&v2, p2 + 4, 4);
                if (t <= q && q <= t2)
                {
                    if (t2 - t < 1e-9f) return v;
                    return v + (q - t) * (v2 - v) / (t2 - t);
                }
            }
        }
        // 末尾まで進んだ = q が最後の区間の後。最後の値を返す
        {
            const uint8_t* p = dat.At(k.first + (avail - 1) * 8 + 4);
            float v = 0.0f;
            if (p != nullptr) memcpy(&v, p, 4);
            return v;
        }
    }

    //------------------------------------------------------------------
    // 0x19 チャンク名の解決ヘルパ
    //------------------------------------------------------------------
    const GeneratorBlock::FfxiKey* FindKey(const GeneratorBlock& g, int type)
    {
        return EffectFfxiFindKey(g.ffxiKeys, type);
    }
}

//======================================================================
//  ffxiB / ffxiC のレコード検索（同 type は最後のものが有効。FUN_004041b0 準拠）
//======================================================================
const GeneratorBlock::FfxiRec* EffectFfxiFindRec(const std::vector<GeneratorBlock::FfxiRec>& v, int type)
{
    const GeneratorBlock::FfxiRec* found = nullptr;
    for (const GeneratorBlock::FfxiRec& r : v)
        if (r.type == type) found = &r;      // 最後のもの
    return found;
}

const GeneratorBlock::FfxiKey* EffectFfxiFindKey(const std::vector<GeneratorBlock::FfxiKey>& v, int type)
{
    const GeneratorBlock::FfxiKey* found = nullptr;
    for (const GeneratorBlock::FfxiKey& k : v)
        if (k.type == type) found = &k;      // 最後のもの
    return found;
}

//======================================================================
//  粒子初期化（FUN_00404750 の 0x5C B 構造体の並び順どおり）
//    type1 → 2 → 3 → 0xB → 0xC → 0x12 → 0x13 → 6 → 8/0x41 → 7 →
//    9 → 0xA → 0xF/0x10/0x11 → 0x1F（円環）→ 方向ブロック → データ3 0x27/0x28
//======================================================================
static void FfxiInitParticle(const GeneratorBlock& g, const DatFile& dat,
                             SubUnit::FfxiPart& pt, int spawnedTotal)
{
    const auto& B = g.ffxiB;

    //--- type1: 初期位置 = rec+0x14..0x1C（ffxitool5 は +0x10 を読まない）---
    if (const GeneratorBlock::FfxiRec* r = EffectFfxiFindRec(B, 0x01))
    {
        if (r->rec != nullptr)
        {
            memcpy(&pt.pos[0], r->rec + 0x14, 4);
            memcpy(&pt.pos[1], r->rec + 0x18, 4);
            memcpy(&pt.pos[2], r->rec + 0x1C, 4);
        }
    }
    //--- type2 / 3: 位置加算（3 = 乱数倍）---
    if (const GeneratorBlock::FfxiRec* r = EffectFfxiFindRec(B, 0x02))
    {
        pt.pos[0] += *(const float*)(r->rec + 4);
        pt.pos[1] += *(const float*)(r->rec + 8);
        pt.pos[2] += *(const float*)(r->rec + 0xC);
    }
    if (const GeneratorBlock::FfxiRec* r = EffectFfxiFindRec(B, 0x03))
    {
        pt.pos[0] += EffectRand01() * *(const float*)(r->rec + 4);
        pt.pos[1] += EffectRand01() * *(const float*)(r->rec + 8);
        pt.pos[2] += EffectRand01() * *(const float*)(r->rec + 0xC);
    }
    //--- type 0xB / 0xC: 回転速度（rotV）---
    if (const GeneratorBlock::FfxiRec* r = EffectFfxiFindRec(B, 0x0B))
    {
        pt.rotV[0] += *(const float*)(r->rec + 4);
        pt.rotV[1] += *(const float*)(r->rec + 8);
        pt.rotV[2] += *(const float*)(r->rec + 0xC);
    }
    if (const GeneratorBlock::FfxiRec* r = EffectFfxiFindRec(B, 0x0C))
    {
        pt.rotV[0] += EffectRand01() * *(const float*)(r->rec + 4);
        pt.rotV[1] += EffectRand01() * *(const float*)(r->rec + 8);
        pt.rotV[2] += EffectRand01() * *(const float*)(r->rec + 0xC);
    }
    //--- type 0x12 / 0x13: 位置速度（posV）---
    if (const GeneratorBlock::FfxiRec* r = EffectFfxiFindRec(B, 0x12))
    {
        pt.posV[0] += *(const float*)(r->rec + 4);
        pt.posV[1] += *(const float*)(r->rec + 8);
        pt.posV[2] += *(const float*)(r->rec + 0xC);
    }
    if (const GeneratorBlock::FfxiRec* r = EffectFfxiFindRec(B, 0x13))
    {
        pt.posV[0] += EffectRand01() * *(const float*)(r->rec + 4);
        pt.posV[1] += EffectRand01() * *(const float*)(r->rec + 8);
        pt.posV[2] += EffectRand01() * *(const float*)(r->rec + 0xC);
    }
    //--- type 6: 散布。ffxitool5 は引数を読まず、単位方向をそのまま加算する
    //    （逆コンパイル: _rand() 1 回破棄 + YPR 乱数 3 角 → pos += dir）---
    if (EffectFfxiFindRec(B, 0x06) != nullptr)
    {
        (void)EffectRand01();                    // 1 回目は破棄（Ghidra 出力どおり）
        float d[3];
        RandDir(d);
        pt.pos[0] += d[0]; pt.pos[1] += d[1]; pt.pos[2] += d[2];
    }
    //--- type 8 / 0x41: 方向ブロック用の速度（0x41 は乱数加算）---
    float speed = 0.0f;
    bool  hasDir = false;
    if (const GeneratorBlock::FfxiRec* r = EffectFfxiFindRec(B, 0x08))
    {
        speed = *(const float*)(r->rec + 4);
        hasDir = true;
    }
    if (const GeneratorBlock::FfxiRec* r = EffectFfxiFindRec(B, 0x41))
    {
        speed += EffectRand01() * *(const float*)(r->rec + 4);
        hasDir = true;
    }
    //--- type 7: 球面散布。dist = (life+1) × unaff_EDI（未確定 → EffectSphDist()）---
    if (const GeneratorBlock::FfxiRec* r7 = EffectFfxiFindRec(B, 0x07))
    {
        (void)r7;
        float d[3];
        RandDir(d);
        const float edi = EffectSphDist();
        const float dist = (float)(g.ffxiLife + 1) * edi;
        pt.pos[0] += d[0] * dist;
        pt.pos[1] += d[1] * dist;
        pt.pos[2] += d[2] * dist;
        pt.posV[0] -= d[0] * edi;
        pt.posV[1] -= d[1] * edi;
        pt.posV[2] -= d[2] * edi;
    }
    //--- type 9 / 0xA: 初期回転 ---
    if (const GeneratorBlock::FfxiRec* r = EffectFfxiFindRec(B, 0x09))
    {
        pt.rot[0] = *(const float*)(r->rec + 4);
        pt.rot[1] = *(const float*)(r->rec + 8);
        pt.rot[2] = *(const float*)(r->rec + 0xC);
    }
    if (const GeneratorBlock::FfxiRec* r = EffectFfxiFindRec(B, 0x0A))
    {
        pt.rot[0] += EffectRand01() * *(const float*)(r->rec + 4);
        pt.rot[1] += EffectRand01() * *(const float*)(r->rec + 8);
        pt.rot[2] += EffectRand01() * *(const float*)(r->rec + 0xC);
    }
    //--- type 0xF / 0x10 / 0x11: 初期スケール（既定 1.0）---
    if (const GeneratorBlock::FfxiRec* r = EffectFfxiFindRec(B, 0x0F))
    {
        pt.scl[0] = *(const float*)(r->rec + 4);
        pt.scl[1] = *(const float*)(r->rec + 8);
        pt.scl[2] = *(const float*)(r->rec + 0xC);
    }
    if (const GeneratorBlock::FfxiRec* r = EffectFfxiFindRec(B, 0x10))
    {
        pt.scl[0] += EffectRand01() * *(const float*)(r->rec + 4);
        pt.scl[1] += EffectRand01() * *(const float*)(r->rec + 8);
        pt.scl[2] += EffectRand01() * *(const float*)(r->rec + 0xC);
    }
    if (const GeneratorBlock::FfxiRec* r = EffectFfxiFindRec(B, 0x11))
    {
        const float v = EffectRand01() * *(const float*)(r->rec + 4);   // 1 乱数を 3 成分へ
        pt.scl[0] += v;
        pt.scl[1] += v;
        pt.scl[2] += v;
    }
    //--- type 0x1F: 円環配置（FUN_00404750 の param_1[0x52b]）---
    //    位相 = spawned × 2π / (div+1)、r = rec+8 + rand×rec+4、
    //    x = cos×r×(rec+0xC)、z = sin×r×(rec+0x14)、y += (rec+0x20) + rand×(rec+0x24)
    if (const GeneratorBlock::FfxiRec* r = EffectFfxiFindRec(B, 0x1F))
    {
        const float a0 = *(const float*)(r->rec + 4);      // 半径の乱数幅
        const float a1 = *(const float*)(r->rec + 8);      // 半径の基準
        const float a2 = *(const float*)(r->rec + 0xC);    // x 方向の倍率
        const float a4 = *(const float*)(r->rec + 0x14);   // z 方向の倍率
        const float a6 = *(const float*)(r->rec + 0x20);   // y の基準
        const float a7 = *(const float*)(r->rec + 0x24);   // y の乱数幅
        const int   div = *(const int*)(r->rec + 0x2C);    // 位相分割数

        const float rad = EffectRand01() * a0 + a1;
        const float phase = (float)spawnedTotal * kTwoPi / (float)(div + 1);
        const float yr = EffectRand01();

        pt.pos[0] = cosf(phase) * a2 * rad + pt.pos[0];
        pt.pos[1] = a6 + yr * a7 + pt.pos[1];
        pt.pos[2] = sinf(phase) * a4 * rad + pt.pos[2];
    }
    //--- 方向ブロック: (type 8 || type 0x41) && type 7 無し →
    //    d = normalize(pos − type1pos)、posV += d × speed ---
    if (hasDir && EffectFfxiFindRec(B, 0x07) == nullptr)
    {
        float d[3] = { pt.pos[0] - g.ffxiPos[0], pt.pos[1] - g.ffxiPos[1], pt.pos[2] - g.ffxiPos[2] };
        const float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (len > 1e-8f)
        {
            d[0] /= len; d[1] /= len; d[2] /= len;
            pt.posV[0] += d[0] * speed;
            pt.posV[1] += d[1] * speed;
            pt.posV[2] += d[2] * speed;
        }
    }
    //--- データ3 0x27 / 0x28: 幅・高さの成長レート（= 粒子[0x15]/[0x16]）---
    if (const GeneratorBlock::FfxiRec* r = EffectFfxiFindRec(g.ffxiC, 0x27))
        pt.wRate = *(const float*)(r->rec + 4);
    if (const GeneratorBlock::FfxiRec* r = EffectFfxiFindRec(g.ffxiC, 0x28))
        pt.hRate = *(const float*)(r->rec + 4);
}

//======================================================================
//  積分（FUN_00403880 の移植）
//    age < life のあいだ、データ3 の type があるときだけ進める:
//      2 → pos += posV / 5 → rot += rotV / 8 → scl += sclV
//      0x27 → w += wRate / 0x28 → h += hRate / age++
//======================================================================
static void FfxiIntegrate(const GeneratorBlock& g, SubUnit::FfxiPart& pt, int life)
{
    if (pt.age >= life) return;

    if (EffectFfxiFindRec(g.ffxiC, 0x02) != nullptr)
    {
        pt.pos[0] += pt.posV[0]; pt.pos[1] += pt.posV[1]; pt.pos[2] += pt.posV[2];
    }
    if (EffectFfxiFindRec(g.ffxiC, 0x05) != nullptr)
    {
        pt.rot[0] += pt.rotV[0]; pt.rot[1] += pt.rotV[1]; pt.rot[2] += pt.rotV[2];
    }
    if (EffectFfxiFindRec(g.ffxiC, 0x08) != nullptr)
    {
        pt.scl[0] += pt.sclV[0]; pt.scl[1] += pt.sclV[1]; pt.scl[2] += pt.sclV[2];
    }
    if (EffectFfxiFindRec(g.ffxiC, 0x27) != nullptr)
        pt.w += pt.wRate;
    if (EffectFfxiFindRec(g.ffxiC, 0x28) != nullptr)
        pt.h += pt.hRate;

    ++pt.age;
}

//======================================================================
//  描画同期（FUN_004050b0 の移植）: SubObject へ書き込んで既存描画経路で描く
//    t01 = age / (life+1)
//      キー 0x21/0x22/0x23 = pos XYZ / 0x24 = rotX（ビルボードのロール）
//      0x25 = rotY / 0x26 = 評価のみ（未使用。ffxitool5 も捨てる）
//      0x27/0x28/0x29 = scale XYZ
//      0x2A = B / 0x2B = G / 0x2C = R / 0x2D = A（ARGB D3DCOLOR 消費のため）
//      0x2E/0x2F = 幅・高さ（形状 0x25 専用の寸法。su.quadHalf へ反映）
//    色: データ2 0x16 rec+4 = RGBA バイト順（byte0=R, 1=G, 2=B, 3=A）
//    合成: データ2 0x1E rec+4 bit2（クリア → DESTBLEND ONE = 加算）
//    0x21 フレーム: idx = floor(t01 × 枚数)（FUN_00403030 は t01 で走査する）
//======================================================================
static void FfxiSyncDraw(const MotionInstance& mi, const GeneratorBlock& g,
                         const DatFile& dat, SubUnit& su)
{
    SubUnit::FfxiPart& pt = su.ffxi;
    SubObject& sub = su.sub;
    const int life = g.ffxiLife;
    const float t01 = (float)pt.age / (float)(life + 1);

    //--- 合成モード（データ2 0x1E rec+4 bit2）---
    if (const GeneratorBlock::FfxiRec* r = EffectFfxiFindRec(g.ffxiB, 0x1E))
    {
        uint32_t flags = 0;
        memcpy(&flags, r->rec + 4, 4);
        pt.addBlend = ((flags & 4) == 0);    // bit2 クリア → DESTBLEND ONE（加算）
    }

    //--- 位置（既定 = 粒子 pos、キー 0x21..0x23 で上書き）---
    float x = pt.pos[0], y = pt.pos[1], z = pt.pos[2];
    if (const GeneratorBlock::FfxiKey* k = FindKey(g, 0x21)) x = EvalFfxiKey(dat, *k, t01);
    if (const GeneratorBlock::FfxiKey* k = FindKey(g, 0x22)) y = EvalFfxiKey(dat, *k, t01);
    if (const GeneratorBlock::FfxiKey* k = FindKey(g, 0x23)) z = EvalFfxiKey(dat, *k, t01);

    //--- 回転（rotX のみビルボードのロールへ。rotY/rotZ は写像しない）---
    float rotX = pt.rot[0];
    if (const GeneratorBlock::FfxiKey* k = FindKey(g, 0x24)) rotX = EvalFfxiKey(dat, *k, t01);
    if (const GeneratorBlock::FfxiKey* k = FindKey(g, 0x25)) (void)EvalFfxiKey(dat, *k, t01);
    if (const GeneratorBlock::FfxiKey* k = FindKey(g, 0x26)) (void)EvalFfxiKey(dat, *k, t01);

    //--- スケール（既定 = 粒子 scl、キー 0x27..0x29 で上書き）---
    float s0 = pt.scl[0], s1 = pt.scl[1], s2 = pt.scl[2];
    if (const GeneratorBlock::FfxiKey* k = FindKey(g, 0x27)) s0 = EvalFfxiKey(dat, *k, t01);
    if (const GeneratorBlock::FfxiKey* k = FindKey(g, 0x28)) s1 = EvalFfxiKey(dat, *k, t01);
    if (const GeneratorBlock::FfxiKey* k = FindKey(g, 0x29)) s2 = EvalFfxiKey(dat, *k, t01);

    //--- 色: 0x16 rec+4 = RGBA バイト順 → ARGB。無ければ中立 0x80808080 ---
    float cr = 0.5f, cg = 0.5f, cb = 0.5f, ca = 0.5f;
    if (const GeneratorBlock::FfxiRec* r = EffectFfxiFindRec(g.ffxiB, 0x16))
    {
        const uint8_t* q = r->rec + 4;
        cr = (float)q[0] / 255.0f;   // byte0 = R
        cg = (float)q[1] / 255.0f;   // byte1 = G
        cb = (float)q[2] / 255.0f;   // byte2 = B
        ca = (float)q[3] / 255.0f;   // byte3 = A（α既定）
    }
    if (const GeneratorBlock::FfxiKey* k = FindKey(g, 0x2A)) cb = EvalFfxiKey(dat, *k, t01);   // B
    if (const GeneratorBlock::FfxiKey* k = FindKey(g, 0x2B)) cg = EvalFfxiKey(dat, *k, t01);   // G
    if (const GeneratorBlock::FfxiKey* k = FindKey(g, 0x2C)) cr = EvalFfxiKey(dat, *k, t01);   // R
    if (const GeneratorBlock::FfxiKey* k = FindKey(g, 0x2D)) ca = EvalFfxiKey(dat, *k, t01);   // A
    // 0..1 にクランプ（負値のキーは 0 = 消灯。例 k006 末尾 -0.01）
    cr = (cr < 0.0f) ? 0.0f : cr; cg = (cg < 0.0f) ? 0.0f : cg;
    cb = (cb < 0.0f) ? 0.0f : cb; ca = (ca < 0.0f) ? 0.0f : ca;

    //--- 幅・高さ（形状 0x25 専用。キー 0x2E/0x2F で上書き）---
    float w = pt.w, h = pt.h;
    if (const GeneratorBlock::FfxiKey* k = FindKey(g, 0x2E)) w = EvalFfxiKey(dat, *k, t01);
    if (const GeneratorBlock::FfxiKey* k = FindKey(g, 0x2F)) h = EvalFfxiKey(dat, *k, t01);

    //--- SubObject へ書き込む（既存描画経路が読む SUB_POS/SUB_SCALE/SUB_COLOR）---
    float* pos = sub.Pos();
    pos[0] = x; pos[1] = y; pos[2] = z;
    float* scl = sub.Scale();
    scl[0] = s0; scl[1] = s1; scl[2] = s2;
    float* col = sub.Color();
    col[0] = cr; col[1] = cg; col[2] = cb; col[3] = ca;
    // 0x21 クワッドの頂点色 × 粒子色 >> 7（FUN_00403030。平均で近似）
    //   cr/cg/cb は平均頂点色（0x80 = 1.0）。ffxi モードの描画側で掛ける
    sub.Elapsed() = (float)pt.age / 60.0f;
    sub.T01()     = t01;
    float* spin = sub.Spin();
    spin[0] = rotX; spin[1] = 0.0f; spin[2] = rotX;   // 描画側は [2] をロールとして使う

    //--- 0x21 フレーム / 0x1F ジオメトリ / 0x25（幅・高さ）の寸法 ---
    const uint8_t shape = g.ffxiShape;
    if (shape == 0x0E && g.HasFrames())
    {
        // idx = floor(t01 × 枚数)（FUN_00403030 は t01 でフレーム表を走査する）
        const int n = (int)g.texFrames.size();
        int idx = (int)(t01 * (float)n);
        if (idx >= n) idx = n - 1;
        if (idx < 0) idx = 0;
        su.frameIdx = idx;
        const TexFrame& f = g.texFrames[idx];
        su.uv[0] = f.u0; su.uv[1] = f.v0; su.uv[2] = f.u1; su.uv[3] = f.v1;
        su.quadHalf[0]   = (f.x1 - f.x0) * 0.5f;
        su.quadHalf[1]   = (f.y1 - f.y0) * 0.5f;
        su.quadCenter[0] = (f.x1 + f.x0) * 0.5f;
        su.quadCenter[1] = (f.y1 + f.y0) * 0.5f;
        su.ffxiFrameColor[0] = f.cr; su.ffxiFrameColor[1] = f.cg; su.ffxiFrameColor[2] = f.cb;
    }
    else if (shape == 0x25)
    {
        su.quadHalf[0] = w * 0.5f;
        su.quadHalf[1] = h * 0.5f;
        su.quadCenter[0] = 0.0f; su.quadCenter[1] = 0.0f;
    }
}

//======================================================================
//  MotionInstance::EvalFfxi（FUN_00404750 の 1 フレーム処理）
//    放出 → 積分 → 寿命圧縮 → 描画同期
//======================================================================
void MotionInstance::EvalFfxi(const EffectContext& ctx, float dt)
{
    (void)ctx;
    const GeneratorBlock* g = gen;
    if (g == nullptr || dat == nullptr) return;

    const int life = g->ffxiLife;
    const int frames = (ffxiFrames > 0) ? ffxiFrames : (int)g->frames + 1;

    //--------------------------------------------------------------
    // フレームアキュムレータ（FUN_004057a0 は 60fps で FUN_00404750 を回す）
    //--------------------------------------------------------------
    ffxiAcc += dt * 60.0f;
    int guard = 0;
    while (ffxiAcc >= 1.0f && guard++ < 32)
    {
        ffxiAcc -= 1.0f;

        //--- 放出（emitAge < window のあいだ。emitAge % frames == 0 で count+1 個）---
        if (ffxiEmitAge < ffxiWindow)
        {
            if (frames > 0 && (ffxiEmitAge % frames) == 0)
            {
                const int batch = (int)g->count + 1;
                for (int i = 0; i < batch; ++i)
                {
                    SubUnit su;
                    su.sub.Clear();
                    FfxiInitParticle(*g, *dat, su.ffxi, ffxiSpawnedTotal);
                    subs.push_back(std::move(su));
                    ++ffxiSpawnedTotal;
                }
            }
            ++ffxiEmitAge;
        }

        //--- 積分（FUN_00403880）---
        for (SubUnit& su : subs)
            FfxiIntegrate(*g, su.ffxi, life);
    }

    //--------------------------------------------------------------
    // 寿命圧縮（FUN_00404750 末尾: age >= life を memmove で詰める）
    //--------------------------------------------------------------
    for (size_t k = 0; k < subs.size(); )
    {
        if (subs[k].ffxi.age >= life)
        {
            subs.erase(subs.begin() + (ptrdiff_t)k);
            continue;
        }
        ++k;
    }

    //--------------------------------------------------------------
    // 描画同期（FUN_004050b0）: SubObject へ書き込んで描画側で使う
    //--------------------------------------------------------------
    for (SubUnit& su : subs)
        FfxiSyncDraw(*this, *g, *dat, su);

    // ffxi モードでは timeline 側の Reset（ループ）がインスタンスを消すので
    // ここでは expired を立てない（放出終了後も残存粒子を描き切る）
}
