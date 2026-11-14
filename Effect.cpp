#include "Effect.h"
#include "EffectBase.h"
#include <cmath>
#include <algorithm>

//======================================================================
//  MotionInstance
//======================================================================
//----------------------------------------------------------------------
//  0x21（テクスチャフレーム）のパース
//
//    +0x12 : u16 フレーム数
//    +0x14 : 0 以外ならエントリヘッダが 0x14 バイト（既定 4）
//    +0x28 : エントリ列。各エントリ = ヘッダ + cnt × 0x90 バイト
//            ヘッダ +2 が cnt。0x90 = 6 頂点 × 24 B（x,y,z,color,u,v）
//----------------------------------------------------------------------
static bool ParseTexFrames(const DatFile& dat, const char* name4, std::vector<TexFrame>& out,
                           char outSpecific[9])
{
    out.clear();
    if (outSpecific != nullptr) outSpecific[0] = '\0';
    if (name4 == nullptr || name4[0] == '\0') return false;

    const DatChunk* c = dat.Find(name4, 0x21);
    if (c == nullptr || c->size < 0x28 || !dat.InRange(c->offset, c->size)) return false;

    const uint8_t* p = dat.At(c->offset);
    if (p == nullptr) return false;

    // +0x20 = specific 名（8 文字）。テクスチャ（0x20）の 16 B 名の後半と一致する
    if (outSpecific != nullptr)
    {
        memcpy(outSpecific, p + 0x20, 8);
        outSpecific[8] = '\0';
        for (int i = 7; i >= 0 && (outSpecific[i] == ' ' || outSpecific[i] == '\0'); --i)
            outSpecific[i] = '\0';
    }

    uint16_t entries = 0;
    memcpy(&entries, p + 0x12, 2);
    const uint32_t hdr = (p[0x14] != 0) ? 0x14u : 4u;

    uint32_t pos = 0x28;
    for (uint16_t e = 0; e < entries; ++e)
    {
        if (pos + hdr + 4 > c->size) break;

        const uint32_t cnt  = p[pos + 2];
        const uint32_t data = pos + hdr;
        if (cnt == 0 || data + 0x90 > c->size) { pos = data; continue; }

        TexFrame f;
        f.u0 = f.v0 = f.x0 = f.y0 =  1e9f;
        f.u1 = f.v1 = f.x1 = f.y1 = -1e9f;

        float csum[3] = { 0.0f, 0.0f, 0.0f };

        for (uint32_t v = 0; v < 6; ++v)
        {
            float q[6] = {};
            memcpy(q, p + data + v * 24, 24);
            if (q[4] < f.u0) f.u0 = q[4];
            if (q[5] < f.v0) f.v0 = q[5];
            if (q[4] > f.u1) f.u1 = q[4];
            if (q[5] > f.v1) f.v1 = q[5];
            if (q[0] < f.x0) f.x0 = q[0];
            if (q[1] < f.y0) f.y0 = q[1];
            if (q[0] > f.x1) f.x1 = q[0];
            if (q[1] > f.y1) f.y1 = q[1];

            // 頂点色（D3DCOLOR = 0xAARRGGBB）。AltanaView は (v * inst) >> 7 = ×2/255
            const uint8_t* cb = reinterpret_cast<const uint8_t*>(p + data + v * 24 + 12);
            csum[0] += cb[2] / 128.0f;   // R
            csum[1] += cb[1] / 128.0f;   // G
            csum[2] += cb[0] / 128.0f;   // B
        }

        f.cr = csum[0] / 6.0f;
        f.cg = csum[1] / 6.0f;
        f.cb = csum[2] / 6.0f;

        out.push_back(f);
        pos = data + cnt * 0x90;
    }
    return !out.empty();
}

//======================================================================
//  0x1F（ジオメトリ）のパース
//    ヘッダ: 0x30 B 固定
//      +0x14 名前数(u8) / +0x16 三角形数(u16) / +0x1E 8 B グループ名
//      +0x26 8 B 別名 / +0x30 から頂点
//    頂点  : 0x30 + i*0x24（36 B、6 頂点で 1 クワッド）
//            +0x00 half3 位置 / +0x1C half u / +0x20 half v
//======================================================================
static void ReadGeoName(const uint8_t* p, GeneratorBlock& g);

static bool ParseGeometry(const DatFile& dat, const char* name4, GeneratorBlock& g)
{
    if (name4 == nullptr || name4[0] == '\0') return false;
    const DatChunk* c = dat.Find(name4, 0x1F);
    if (c == nullptr || c->size < 0x40 || !dat.InRange(c->offset, c->size)) return false;

    const uint8_t* p = dat.At(c->offset);
    if (p == nullptr) return false;

    uint16_t shapes = 0;
    memcpy(&shapes, p + 0x16, 2);
    if (shapes == 0 || shapes > 4096) return false;

    const uint32_t start = 0x30;     // 実測: `f101` で 0x30 + count*3*0x24 = 0xDB0（末尾一致）
    const uint32_t stride = 0x24;
    uint32_t verts = (uint32_t)shapes * 3u;             // 三角形数 × 3
    // チャンクサイズを超える場合は「入る分だけ」読む（未解析領域を頂点として読むのを防ぐ）
    const uint32_t maxVerts = (c->size > start) ? (uint32_t)((c->size - start) / stride) : 0u;
    if (maxVerts < 6u) return false;
    if (verts > maxVerts) verts = maxVerts;
    verts -= verts % 6u;                                 // 6 頂点（2 三角形）単位に揃える
    if (verts < 6u) return false;

    // 位置は half-float（実測 0x3F00=1.75 / 0xBF00=-1.75）
    auto half2f = [](uint16_t h) -> float
    {
        const int s = (h >> 15) & 1;
        const int e = (h >> 10) & 0x1F;
        const int m = h & 0x3FF;
        float v;
        if (e == 0)      v = (float)m * 5.9604644775390625e-8f;      // 2^-24
        else if (e == 31) v = (m == 0) ? 1e30f : 0.0f;
        else             v = (1.0f + (float)m / 1024.0f) * powf(2.0f, (float)(e - 15));
        return s ? -v : v;
    };

    // UV は half（実測: `fire` の 4 隅 = (1.871,0)/(1.871,1.871)/(0,0)/(0,1.871)）
    //   +0x1C = u / +0x20 = v（float32 ではない）
    bool axisDisabled[3] = { false, false, false };
    struct Vtx { float x, y, z, u, v; };
    auto ReadVtx = [&](uint32_t idx, Vtx& o)
    {
        const uint8_t* q = p + start + idx * stride;
        uint16_t hx = 0, hy = 0, hz = 0, hu = 0, hv = 0;
        memcpy(&hx, q + 0x00, 2);
        memcpy(&hy, q + 0x02, 2);
        memcpy(&hz, q + 0x04, 2);
        memcpy(&hu, q + 0x1C, 2);
        memcpy(&hv, q + 0x20, 2);
        o.x = half2f(hx); o.y = half2f(hy); o.z = half2f(hz);
        o.u = half2f(hu); o.v = half2f(hv);
    };

    // 未初期化成分の判定:
    //   285 チャンクを実測すると、ある軸にだけ |値|>=100 の異常値が混入する
    //   （`syur` y: 90/144 = 63%、`ic` y: 16/90 = 18%、`m1` y: 10/12 = 83%、
    //     `fire` y: 0/6）。1 軸の異常率が 30% を超えるチャンクは、
    //   その軸が「書かれていない（未初期化）」と判断して 0 固定にする
    {
        const uint32_t axes[3] = { 0x00u, 0x02u, 0x04u };
        for (int axis = 0; axis < 3; ++axis)
        {
            uint32_t junk = 0;
            for (uint32_t i = 0; i < verts; ++i)
            {
                const uint8_t* q = p + start + i * stride + axes[axis];
                uint16_t h = 0;
                memcpy(&h, q, 2);
                const float v = half2f(h);
                if (!(v > -100.0f && v < 100.0f)) ++junk;
            }
            if (verts > 0 && (float)junk / (float)verts > 0.30f)
            {
                axisDisabled[axis] = true;
            }
        }
    }

    // 6 頂点（2 三角形）ごとに 1 枚の平行四辺形を作る
    //   A=v0, B=v1, C=v2 の 3 頂点から平面を作る（A' = B + C - A）
    //   妥当性は「辺の長さ」で判定し、異常なクワッドだけ捨てる
    g.geoQuads.clear();
    for (uint32_t s = 0; s + 6u <= verts; s += 6u)
    {
        Vtx A, B, C, D, E, F;
        ReadVtx(s + 0, A);
        ReadVtx(s + 1, B);
        ReadVtx(s + 2, C);
        ReadVtx(s + 3, D);
        ReadVtx(s + 4, E);
        ReadVtx(s + 5, F);

        auto ok = [](float v) { return (v > -100.0f && v < 100.0f); };

        // 未初期化成分（|v|>1000）は、同じクワッド内の有効値の中央値で補う
        //   実測: 285 チャンク中 71 で 1 軸に異常値が混入（例: `syur` y=6.1e4 が 84/144 個）
        Vtx vs[6] = { A, B, C, D, E, F };
        for (int k = 0; k < 6; ++k)
        {
            if (axisDisabled[0]) vs[k].x = 0.0f;
            if (axisDisabled[1]) vs[k].y = 0.0f;
            if (axisDisabled[2]) vs[k].z = 0.0f;
        }
        float med[3] = { 0.0f, 0.0f, 0.0f };
        for (int axis = 0; axis < 3; ++axis)
        {
            if (axisDisabled[axis]) continue;
            float tmp[6];
            int n = 0;
            for (int k = 0; k < 6; ++k)
            {
                const float val = (axis == 0) ? vs[k].x : (axis == 1) ? vs[k].y : vs[k].z;
                if (ok(val)) tmp[n++] = val;
            }
            if (n > 0)
            {
                std::sort(tmp, tmp + n);
                med[axis] = tmp[n / 2];
            }
        }
        for (int k = 0; k < 6; ++k)
        {
            if (!axisDisabled[0] && !ok(vs[k].x)) vs[k].x = med[0];
            if (!axisDisabled[1] && !ok(vs[k].y)) vs[k].y = med[1];
            if (!axisDisabled[2] && !ok(vs[k].z)) vs[k].z = med[2];
        }
        A = vs[0]; B = vs[1]; C = vs[2];

        const float rx = (B.x - A.x) * 0.5f, ry = (B.y - A.y) * 0.5f, rz = (B.z - A.z) * 0.5f;
        const float ux = (C.x - A.x) * 0.5f, uy = (C.y - A.y) * 0.5f, uz = (C.z - A.z) * 0.5f;
        const float rLen = sqrtf(rx * rx + ry * ry + rz * rz);
        const float uLen = sqrtf(ux * ux + uy * uy + uz * uz);
        if (rLen < 1e-4f || uLen < 1e-4f) continue;          // 退化
        if (rLen > 50.0f || uLen > 50.0f) continue;          // 異常に大きい

        GeneratorBlock::GeoQuad gq;
        gq.ctr[0] = A.x + rx + ux;
        gq.ctr[1] = A.y + ry + uy;
        gq.ctr[2] = A.z + rz + uz;
        gq.aR[0] = rx; gq.aR[1] = ry; gq.aR[2] = rz;
        gq.aU[0] = ux; gq.aU[1] = uy; gq.aU[2] = uz;
        gq.uv[0] = A.u; gq.uv[1] = A.v;
        gq.uv[2] = B.u + C.u - A.u;
        gq.uv[3] = B.v + C.v - A.v;
        g.geoQuads.push_back(gq);
    }
    if (g.geoQuads.empty()) return false;

    // 代表値（ログ・--geoflat 用）は有効なクワッドの union から作る
    {
        float hx0 = 1e9f, hy0 = 1e9f, hz0 = 1e9f, hx1 = -1e9f, hy1 = -1e9f, hz1 = -1e9f;
        for (const GeneratorBlock::GeoQuad& gq : g.geoQuads)
        {
            for (int sx = -1; sx <= 1; sx += 2)
            {
                for (int sy = -1; sy <= 1; sy += 2)
                {
                    const float px = gq.ctr[0] + gq.aR[0] * (float)sx + gq.aU[0] * (float)sy;
                    const float py = gq.ctr[1] + gq.aR[1] * (float)sx + gq.aU[1] * (float)sy;
                    const float pz = gq.ctr[2] + gq.aR[2] * (float)sx + gq.aU[2] * (float)sy;
                    if (px < hx0) hx0 = px;
                    if (py < hy0) hy0 = py;
                    if (pz < hz0) hz0 = pz;
                    if (px > hx1) hx1 = px;
                    if (py > hy1) hy1 = py;
                    if (pz > hz1) hz1 = pz;
                }
            }
        }
        g.hasGeo = true;
        g.geoHalf[0]   = (hx1 - hx0) * 0.5f;
        g.geoHalf[1]   = (hy1 - hy0) * 0.5f;
        g.geoCenter[0] = (hx1 + hx0) * 0.5f;
        g.geoCenter[1] = (hy1 + hy0) * 0.5f;
        // 水平（XZ 平面）か: 有効クワッド全体の Y 厚みがほぼ 0 なら地面デカール
        g.geoFlat = ((hy1 - hy0) < 0.01f && ((hx1 - hx0) > 0.05f || (hz1 - hz0) > 0.05f));
        // UV の代表レンジ（全クワッドの union）
        float tu0 = 1e9f, tv0 = 1e9f, tu1 = -1e9f, tv1 = -1e9f;
        for (const GeneratorBlock::GeoQuad& gq : g.geoQuads)
        {
            for (int k = 0; k < 4; k += 2)
            {
                if (gq.uv[k + 0] < tu0) tu0 = gq.uv[k + 0];
                if (gq.uv[k + 1] < tv0) tv0 = gq.uv[k + 1];
                if (gq.uv[k + 0] > tu1) tu1 = gq.uv[k + 0];
                if (gq.uv[k + 1] > tv1) tv1 = gq.uv[k + 1];
            }
        }
        if (tu1 - tu0 > 1e-4f && tv1 - tv0 > 1e-4f)
        {
            g.geoUV[0] = tu0; g.geoUV[1] = tv0; g.geoUV[2] = tu1; g.geoUV[3] = tv1;
        }
        else
        {
            g.geoUV[0] = 0.0f; g.geoUV[1] = 0.0f; g.geoUV[2] = 1.0f; g.geoUV[3] = 1.0f;
        }
    }
    ReadGeoName(p, g);
    return true;
}

// テクスチャ名: 0x1E に 8 文字×2（グループ名 + specific 名）＝ 0x20 の 16 バイト名と同じ
//   実測: 'fire'  → "fire1   " + "fire16  "
//         'f101'  → "firef1  " + "f10f    "
static void ReadGeoName(const uint8_t* p, GeneratorBlock& g)
{
    memcpy(g.geoName16, p + 0x1E, 16);
    g.geoName16[16] = '\0';
    memcpy(g.geoName, p + 0x1E, 8);
    g.geoName[8] = '\0';
    for (int i = 7; i >= 0 && (g.geoName[i] == ' ' || g.geoName[i] == '\0'); --i) g.geoName[i] = '\0';
}

void MotionInstance::Reset(const DatFile* d, const GeneratorBlock* g, float start, int tex, int op, bool applyGeo)
{
    dat       = d;
    gen       = g;
    startTime = start;
    expired   = false;
    texIndex  = tex;
    spawnOp   = op;

    // 粒子数 = ジェネレータの +0x78 + 1（AltanaView の MotionObject.count と同じ）
    const int n = (g != nullptr) ? (int)g->count + 1 : 1;
    subs.clear();

    // 連続放出のパラメータ（寿命は Vm2 が ctx.Duration() から決める）
    emitTotal    = (n > 0) ? n : 1;
    emitSpawned  = 0;
    emitInterval = 0.0f;      // 最初の Eval で寿命から計算する
    emitTimer    = 0.0f;

    // 従来モード（--emit off）は t=0 で全部そろえる（旧挙動の再現用）
    if (!EffectEmitMode())
    {
        subs.assign((size_t)emitTotal, SubUnit{});
        for (SubUnit& su : subs) InitSub(su, applyGeo);
        emitSpawned = emitTotal;
    }
}

// 1 個ぶんのサブ状態を初期化する（Reset のループ内容を関数化）
void MotionInstance::InitSub(SubUnit& su, bool applyGeo)
{
    su.frameIdx = 0;
    su.uv[0] = 0.0f; su.uv[1] = 0.0f; su.uv[2] = 1.0f; su.uv[3] = 1.0f;

    su.sub.Clear();
    su.sub.Elapsed() = 0.0f;
    su.sub.T01()     = 0.0f;
    // キー補間カーソルも初期化（元実装はスポーン時に新しい粒子を作る）
    for (int i = 0; i < (int)SUB_DEST_N; ++i) su.keyCur[i] = 0;
    float* prev = su.sub.At(SUB_PREV);
    prev[0] = prev[1] = prev[2] = 0.0f;

    // 0x1F（ジオメトリ）: bbox を寸法・UV として使う（--geo on のときのみ）
    if (applyGeo && gen != nullptr && gen->hasGeo)
    {
        su.quadHalf[0]   = gen->geoHalf[0];
        su.quadHalf[1]   = gen->geoHalf[1];
        su.quadCenter[0] = gen->geoCenter[0];
        su.quadCenter[1] = gen->geoCenter[1];
        su.uv[0] = gen->geoUV[0]; su.uv[1] = gen->geoUV[1];
        su.uv[2] = gen->geoUV[2]; su.uv[3] = gen->geoUV[3];
    }
}

// 粒子を 1 個生成する（AltanaView: 放出時に FUN_0040e650 = VM#1 を 1 回だけ実行）
// 寿命到達時の再初期化（AltanaView FUN_00410090 の移植）
//   ※ 実装は EffectVm.cpp（Vm1 / Rand01 を使うため）

//======================================================================
//  EffectContext : 構築
//======================================================================
bool EffectContext::Build(const DatFile& dat)
{
    return BuildInternal(dat, "main");
}

// 任意の 0x07 ブロック（NPC のアクション名など）をタイムラインとして構築する
bool EffectContext::BuildFromChunk(const DatFile& dat, const char* chunk4)
{
    return BuildInternal(dat, chunk4);
}

//======================================================================
//  0x07 ブロックの命令列位置
//    Effect / NPC 共通で **+0x50 から**始まり、len==0 で終端する
//    （2026-11-09 解析: 1,848 チャンク中 1,806（97.7%）が +0x50 から正常終端）
//======================================================================

//======================================================================
//  索引のクリア（問題 3）
//    再構築（Build / BuildFromChunk）で消し忘れると、テクスチャ番号が
//    m_Textures とずれて誤ったテクスチャ（または白）になる。
//    「テクスチャ索引系」は必ずこの 1 か所でまとめてクリアする。
//======================================================================
void EffectContext::ClearIndex()
{
    m_Timeline.clear();
    m_Generators.clear();
    m_Textures.clear();
    m_TexNames.clear();
    m_TexSpecific.clear();      // ← 以前はクリア漏れ（m_TexNames とずれていた）
    m_Instances.clear();
    m_Subs.clear();             // 入れ子 Schedule も旧 DAT を指したままにしない
    m_BaseGens.clear();         // base 0x05 の変換キャッシュ
    m_BaseSubs = 0;
    m_MissingGenerators = 0;
}

bool EffectContext::BuildInternal(const DatFile& dat, const char* mainChunkName)
{
    m_Dat = &dat;
    ClearIndex();
    m_Duration = 0.0f;
    m_ProgStart = 0;

    //--------------------------------------------------------------
    // 1) ジェネレータ（type 0x05）索引
    //--------------------------------------------------------------
    for (const DatChunk& c : dat.Chunks())
    {
        if (c.type == 0x20)
        {
            m_TexNames.push_back(c.name);                    // texIndex の基準順

            // 0x20 の 16 B 名（+17）は「グループ名 8 + specific 名 8」。
            // specific 名は 0x21 の +0x20 が指す名前で、テクスチャ解決に使う。
            std::string spec;
            if (dat.InRange(c.offset + 17, 16))
            {
                const uint8_t* tn = dat.At(c.offset + 17);
                spec.assign(reinterpret_cast<const char*>(tn) + 8, 8);
                while (!spec.empty() && (spec.back() == ' ' || spec.back() == '\0')) spec.pop_back();
            }
            m_TexSpecific.push_back(spec);
            continue;
        }
        if (c.type != 0x05) continue;
        if (c.size <= 0x90) continue;                       // ヘッダ 0x90 に満たない
        if (!dat.InRange(c.offset, c.size)) continue;

        GeneratorBlock g;
        g.chunk       = c;
        g.payloadOff  = c.offset + 0x90;
        g.payloadSize = c.size - 0x90;

        const uint8_t* p = dat.At(c.offset);
        uint32_t v = 0;
        memcpy(&v, p + 0x10, 4); g.id = v;
        uint16_t f16 = 0; memcpy(&f16, p + 0x76, 2); g.frames = f16;
        g.count = p[0x78];
        g.flag  = p[0x79];
        if (c.size > 0xB0)
        {
            memcpy(g.linkName, p + 0xAC, 4);
            g.linkName[4] = '\0';
        }

        // 同名の 0x21（テクスチャフレーム）があれば UV 矩形と specific 名を取り込む
        ParseTexFrames(dat, g.linkName, g.texFrames, g.texName);

        // 同名の 0x1F（ジオメトリ）があれば bbox と UV・テクスチャ名を取り込む
        ParseGeometry(dat, g.linkName, g);

        uint32_t o = 0;
        memcpy(&o, p + 0x80, 4); g.tblA = g.payloadOff + (o - 0x90);
        memcpy(&o, p + 0x84, 4); g.tblB = g.payloadOff + (o - 0x90);
        memcpy(&o, p + 0x88, 4); g.tblC = g.payloadOff + (o - 0x90);
        memcpy(&o, p + 0x8C, 4); g.tblD = g.payloadOff + (o - 0x90);

        // データ2 の先頭 op 0x01（48 B）: bil / テクスチャ名 / 位置オフセット
        //   参考: cosmosa.jp/ffxitool/generator.html
        //     WORD bil (0=平面, 1=ビルボード), LONG ID, LONG dmy, float x,y,z …
        //   実測: 11\17 g000 → 01 0C 00 00 | 01 00 | … | 'fai1' | 0 | 0 | -0.1
        if (dat.InRange(g.tblB, 48))
        {
            const uint8_t* q = dat.At(g.tblB);
            if (q != nullptr && q[0] == 0x01 && (q[1] & 0x1F) >= 11)
            {
                uint16_t bil = 1;
                memcpy(&bil, q + 0x04, 2);
                g.bil = (int)bil;

                // +0x06 はアタッチ部位 ID（参考: 0x001=中心, 0x081=右腕, 0x0C1=頭 …）
                uint16_t pid = 0;
                memcpy(&pid, q + 0x06, 2);
                g.partId = (int)pid;
                memcpy(g.partName, q + 0x0C, 4);
                g.partName[4] = '\0';
                memcpy(&g.partOfs[0], q + 0x10, 4);
                memcpy(&g.partOfs[1], q + 0x14, 4);
                memcpy(&g.partOfs[2], q + 0x18, 4);
                // +0x22 は LifeTime（フレーム数。参考ページ: 60 で 1 秒）
                uint16_t life = 0;
                memcpy(&life, q + 0x22, 2);
                g.lifeFrames = (int)life;

                // 妥当な範囲（±10 m）のみ有効とする
                g.hasPartOfs = (g.partOfs[0] > -10.0f && g.partOfs[0] < 10.0f &&
                                g.partOfs[1] > -10.0f && g.partOfs[1] < 10.0f &&
                                g.partOfs[2] > -10.0f && g.partOfs[2] < 10.0f);
            }
        }

        if (!dat.InRange(g.tblA, 1)) g.tblA = g.payloadOff;
        if (!dat.InRange(g.tblB, 1)) g.tblB = g.payloadOff;
        if (!dat.InRange(g.tblC, 1)) g.tblC = g.payloadOff;
        if (!dat.InRange(g.tblD, 1)) g.tblD = g.payloadOff;

        m_Generators.push_back(g);
    }

    //--------------------------------------------------------------
    // 2) タイムライン（main / type 0x07）
    //--------------------------------------------------------------
    const DatChunk* mainChunk = dat.Find(mainChunkName, 0x07);
    if (mainChunk == nullptr) mainChunk = dat.FindByType(0x07);

    if (mainChunk != nullptr)
    {
        // 0x07 の命令列は +0x50 から始まる（Effect / NPC 共通。解析 §8 / 設計 §4.8.2）
        //   実測: 1,848 チャンク中 1,806（97.7%）が +0x50 から len==0 で正常終端
        const uint32_t progStart = 0x50;
        m_ProgStart = progStart;
        if (mainChunk->size <= progStart)
        {
            Reset();
            return !m_Generators.empty();
        }

        const uint32_t start = mainChunk->offset + progStart;  // 命令列の開始
        const uint32_t end   = mainChunk->End();

        uint32_t pos = start;
        while (pos + 8 <= end)
        {
            const uint8_t* p = dat.At(pos);
            if (p == nullptr) break;

            TimelineInst in = {};
            in.op  = p[0];
            in.len = p[1] & 0x1F;
            if (in.len == 0) break;                          // 終端（0 埋め）
            memcpy(&in.delay, p + 4, 2);
            memcpy(&in.param, p + 6, 2);
            memcpy(in.name,   p + 8, 4);
            in.name[4] = '\0';
            memcpy(in.rgba,   p + 8, 4);
            in.extra  = (pos + 0x1E < end) ? p[0x1E] : 0;
            in.offset = pos - mainChunk->offset;

            m_Timeline.push_back(in);
            m_Duration += in.delay / 60.0f;

            if (in.op == 0x00) break;                        // 明示終端
            pos += (uint32_t)in.len * 4;
        }

        //----------------------------------------------------------
        // ループ（サブ再初期化）フラグ
        //   AltanaView FUN_0040d790: main のストリーム D(+0x8C) に op 0x05 が
        //   現れると root+0x5e が立ち、寿命でサブを削除せず再初期化する
        //----------------------------------------------------------
        m_LoopRespawn = false;
        if (mainChunk->size > 0x8C && dat.InRange(mainChunk->offset, mainChunk->size))
        {
            uint32_t relD = 0;
            memcpy(&relD, dat.At(mainChunk->offset + 0x8C), 4);
            uint32_t pD = mainChunk->offset + relD;
            for (int k = 0; k < 64; ++k)
            {
                if (pD + 4 > end) break;
                const uint8_t* q = dat.At(pD);
                if (q == nullptr) break;
                const uint8_t op = q[0];
                const uint8_t len = q[1] & 0x1F;
                if (op == 0x00 || len == 0) break;
                if (op == 0x05) { m_LoopRespawn = true; break; }
                pD += (uint32_t)len * 4;
            }
        }
    }

    Reset();
    return !m_Timeline.empty() || !m_Generators.empty();
}

bool EffectContext::BuildTextures(ID3D11Device* device)
{
    ReleaseTextures();
    if (device == nullptr || m_Dat == nullptr) return false;

    for (const DatChunk& c : m_Dat->Chunks())
    {
        if (c.type != 0x20) continue;
        TimTexture t;
        if (!CreateTimTexture(device, m_Dat->At(c.offset), c.size, t))
        {
            t.name = c.name;      // 失敗しても index を揃えるため名前だけ保持（srv は null）
        }
        m_Textures.push_back(t);
    }
    CreateWhiteTexture(device, m_WhiteTex);
    return true;
}

void EffectContext::ReleaseTextures()
{
    for (TimTexture& t : m_Textures) t.Release();
    m_Textures.clear();
    m_WhiteTex.Release();
}

//======================================================================
//  EffectContext : 再生
//======================================================================
void EffectContext::Reset()
{
    m_Ip       = 0;
    m_TimeLeft = 0.0f;
    m_Frame    = 0;
    m_Finished = false;
    m_Instances.clear();
    m_Subs.clear();
}

void EffectContext::Advance(float dt)
{
    if (m_Finished && !m_Loop) return;

    const float sdt = dt * m_Speed;
    if (sdt <= 0.0f) return;

    //--------------------------------------------------------------
    // 1) タイムライン（FUN_004103d0 と同じ時間の進め方）
    //--------------------------------------------------------------
    m_TimeLeft -= sdt;
    int guard = 0;
    while (m_TimeLeft < 0.0f && guard++ < 1024)
    {
        if (m_Ip >= m_Timeline.size()) { m_Finished = true; break; }

        const TimelineInst in = m_Timeline[m_Ip];
        if (in.op == 0x00) { m_Finished = true; break; }

        ExecInst(in);
        m_TimeLeft += in.delay / 60.0f;      // ★命令の delay を加算
        ++m_Ip;
    }

    //--------------------------------------------------------------
    // 1b) 入れ子 Schedule（op 0x03 で起動された別の 0x07）
    //--------------------------------------------------------------
    for (SubTimeline& s : m_Subs)
    {
        s.timeLeft -= sdt;
        int guard2 = 0;
        while (s.timeLeft < 0.0f && guard2++ < 1024)
        {
            if (s.ip >= s.insts.size()) break;

            const TimelineInst in = s.insts[s.ip];
            if (in.op == 0x00) break;

            ExecSub(s, in);
            s.timeLeft += in.delay / 60.0f;
            ++s.ip;
        }
    }

    //--------------------------------------------------------------
    // 2) 生成済みインスタンスの更新（FUN_0040e600 相当）
    //--------------------------------------------------------------
    for (MotionInstance& mi : m_Instances)
    {
        if (mi.expired) continue;
        mi.Eval(*this, sdt);
    }

    // 寿命切れを除去（問題 4: 期限切れがあるときだけ詰める。毎フレームの深コピーを避ける）
    if (!m_Instances.empty())
    {
        const bool anyExpired =
            std::any_of(m_Instances.begin(), m_Instances.end(),
                        [](const MotionInstance& mi) { return mi.expired; });
        if (anyExpired)
        {
            m_Instances.erase(std::remove_if(m_Instances.begin(), m_Instances.end(),
                                             [](const MotionInstance& mi) { return mi.expired; }),
                              m_Instances.end());
        }
    }

    ++m_Frame;

    if (m_Finished && m_Loop) Reset();
}

void EffectContext::ExecInst(const TimelineInst& in)
{
    switch (in.op)
    {
    case 0x02:      // モーション生成
        Spawn(in.name, in.param / 60.0f, (int)in.op);
        break;

    case 0x03:      // 別種生成: 0x07（別 Schedule）なら入れ子で実行、0x05 なら従来どおり生成
        if (!StartSubTimeline(in.name))
            Spawn(in.name, in.param / 60.0f, (int)in.op);
        break;

    case 0x2A:      // 色（inline RGBA）
        for (int i = 0; i < 4; ++i) m_LastColor[i] = in.rgba[i];
        break;

    case 0x3D:      // '=' ブロック開始 → 対応する '>' までスキップ
        while (m_Ip < m_Timeline.size() && m_Timeline[m_Ip].op != 0x3E) ++m_Ip;
        break;

    default:
        break;      // 0x05(モデル生成) / 0x18 / 0x25 / サウンド系は未実装
    }
}

// 0x07 チャンクの命令列（+0x50 起点）を読み出す
bool EffectContext::ParseTimeline(const DatFile& dat, const DatChunk& c, std::vector<TimelineInst>* out) const
{
    if (out == nullptr) return false;
    out->clear();
    if (c.size <= 0x50) return false;

    const uint32_t start = c.offset + 0x50;
    const uint32_t end   = c.End();
    uint32_t pos = start;
    while (pos + 8 <= end)
    {
        const uint8_t* p = dat.At(pos);
        if (p == nullptr) break;

        TimelineInst in = {};
        in.op  = p[0];
        in.len = p[1] & 0x1F;
        if (in.len == 0) break;

        memcpy(&in.delay, p + 4, 2);
        memcpy(&in.param, p + 6, 2);
        memcpy(in.name,   p + 8, 4);
        in.name[4] = '\0';
        memcpy(in.rgba,   p + 8, 4);
        in.extra  = (pos + 0x1E < end) ? p[0x1E] : 0;
        in.offset = pos - c.offset;

        out->push_back(in);
        if (in.op == 0x00) break;
        pos += (uint32_t)in.len * 4;
    }
    return !out->empty();
}

// op 0x03 の名前が 0x07（別 Schedule）を指すとき、その命令列を入れ子で起動する
bool EffectContext::StartSubTimeline(const char* name4)
{
    if (m_Dat == nullptr) return false;
    return StartSubTimelineFrom(*m_Dat, name4);
}

// 0x07 の解決順: 呼び出し元の DAT → base DAT（ROM\0\0.DAT）
//   Cure 10/9 の tgt0 は `op=0x03 name=mdam` を持ち、mdam は **base の 0x07**
//   （base offset 0x81780 / 128 B、命令は 0x01, 0x2B, 0x00）。
//   ffxitool5 も DAT 外の参照を解決するため、base を第 2 候補にする。
bool EffectContext::StartSubTimelineFrom(const DatFile& src, const char* name4)
{
    if (name4 == nullptr) return false;
    if (m_Subs.size() >= 16) return false;                       // 暴走防止

    for (const SubTimeline& s : m_Subs)
        if (memcmp(s.name.c_str(), name4, 4) == 0) return true;   // 起動済み

    const DatChunk* c = src.Find(name4, 0x07);
    const DatFile*  owner = &src;
    bool fromBase = false;
    if (c == nullptr && m_Base != nullptr && &src != &m_Base->Dat())
    {
        c = m_Base->FindTimeline(name4);
        if (c != nullptr) { owner = &m_Base->Dat(); fromBase = true; }
    }
    if (c == nullptr) return false;

    SubTimeline s;
    s.name.assign(name4, 4);
    s.dat = owner;
    if (!ParseTimeline(*owner, *c, &s.insts)) return false;

    if (fromBase) ++m_BaseSubs;

    // 起動した Schedule の命令列（op のみ）を残す。base の 0x2B のように
    //   当方が未実装の op が含まれる場合、ここで気付けるようにする。
    char ops[128] = {};
    size_t w = 0;
    for (const TimelineInst& t : s.insts)
    {
        if (w + 4 >= sizeof(ops)) break;
        sprintf_s(ops + w, sizeof(ops) - w, "%02X ", (unsigned)t.op);
        w += 3;
        if (t.op == 0x00) break;
    }
    EffectLogLine("[sub] %s: %s insts=%zu at frame=%d ops=%s", name4, fromBase ? "base" : "local",
                  s.insts.size(), (int)m_Frame, ops);
    m_Subs.push_back(std::move(s));
    return true;
}

// base DAT の 0x05 → GeneratorBlock（テクスチャフレームと 0x1F は未解析）
//   MotionInstance は gen ポインタを保持するので、unique_ptr の安定領域に置く。
const GeneratorBlock* EffectContext::FindBaseGenerator(const char* name4)
{
    if (m_Base == nullptr || name4 == nullptr) return nullptr;

    for (const std::unique_ptr<GeneratorBlock>& p : m_BaseGens)
        if (p && memcmp(p->chunk.name, name4, 4) == 0) return p.get();

    const BaseGenerator* bg = m_Base->FindGenerator(name4);
    if (bg == nullptr) return nullptr;

    std::unique_ptr<GeneratorBlock> g(new GeneratorBlock());
    g->chunk       = bg->chunk;
    g->payloadOff  = bg->payloadOff;
    g->payloadSize = bg->payloadSize;
    g->tblA = bg->tblA; g->tblB = bg->tblB; g->tblC = bg->tblC; g->tblD = bg->tblD;
    g->id     = bg->id;
    g->frames = bg->frames;
    g->count  = bg->count;
    g->flag   = bg->flag;
    memcpy(g->linkName, bg->linkName, sizeof(g->linkName));

    m_BaseGens.push_back(std::move(g));
    EffectLogLine("[spawn] base generator: %s (link=%s)", name4, m_BaseGens.back()->linkName);
    return m_BaseGens.back().get();
}

void EffectContext::ExecSub(SubTimeline& s, const TimelineInst& in)
{
    const DatFile* sdat = (s.dat != nullptr) ? s.dat : m_Dat;

    switch (in.op)
    {
    case 0x02:
        Spawn(in.name, in.param / 60.0f, (int)in.op, sdat);
        break;

    case 0x03:      // 入れ子 Schedule（base の 0x07 も対象）
        if (sdat == nullptr || !StartSubTimelineFrom(*sdat, in.name))
            Spawn(in.name, in.param / 60.0f, (int)in.op, sdat);
        break;

    case 0x2A:
        for (int i = 0; i < 4; ++i) m_LastColor[i] = in.rgba[i];
        break;

    case 0x3D:      // '=' ブロック → '>' までスキップ（入れ子側の ip を進める）
        while (s.ip < s.insts.size() && s.insts[s.ip].op != 0x3E) ++s.ip;
        break;

    default:
        break;
    }
}

void EffectContext::Spawn(const char* name4, float startSec, int op, const DatFile* src)
{
    const DatFile* d = (src != nullptr) ? src : m_Dat;

    // 生成元 DAT がローカルならローカル索引、base の Schedule からなら base の 0x05 を引く
    const GeneratorBlock* g = nullptr;
    if (d == m_Dat)
        g = FindGenerator(name4);
    else if (d != nullptr)
        g = FindBaseGenerator(name4);

    if (g == nullptr)
    {
        // 問題 6: 黙って return すると「表示が消えるだけ」で原因が分からない。
        //   ベース DAT へのフォールバックは未実装（設計判断 D-8）なので、
        //   少なくとも未解決であることをログに残す。
        EffectLogLine("[spawn] generator not found: %s (dat=%s op=0x%02X)",
                      (name4 != nullptr) ? name4 : "(null)",
                      (m_Dat != nullptr) ? m_Dat->Path().c_str() : "(none)", op & 0xFF);
        ++m_MissingGenerators;
        return;
    }

    MotionInstance mi;
    // テクスチャ解決: 0x21 の specific 名（あれば）→ +0xAC のリンク名 の順
    int tex = -1;
    if (g->texName[0] != '\0') tex = FindTextureIndexBySpecific(g->texName);
    if (tex < 0) tex = FindTextureIndex(g->linkName);
    if (tex < 0 && g->geoName16[0] != '\0') tex = FindTextureIndexBy16(g->geoName16);
    if (tex < 0 && g->geoName[0] != '\0') tex = FindTextureIndexByGroup(g->geoName);
    if (tex < 0 && g->geoName[0] != '\0') tex = FindTextureIndexBySpecific(g->geoName);
    mi.Reset(d, g, startSec, tex, op, m_Geo);

    // 描画に使う SRV（借用）: ローカル 0x20 → base の 0x20 の順で解決
    if (tex >= 0 && tex < (int)m_Textures.size())
    {
        mi.srv = m_Textures[tex].srv;
        mi.blackRatio = m_Textures[tex].blackRatio;
    }
    else if (m_Base != nullptr && g->linkName[0] != '\0')
        mi.srv = m_Base->FindTexture(g->linkName);

    // 直近の色（op 0x2A）を初期色として与える（全サブへ）
    for (SubUnit& su : mi.subs)
    {
        float* col = su.sub.Color();
        col[0] = m_LastColor[0] / 255.0f;
        col[1] = m_LastColor[1] / 255.0f;
        col[2] = m_LastColor[2] / 255.0f;
        col[3] = m_LastColor[3] / 255.0f;

        // 0x21 を持たないジェネレータ: テクスチャの不透明領域を UV にする（--uvcrop on）
        if (m_UvCrop && !g->HasFrames() && tex >= 0 && tex < (int)m_Textures.size())
        {
            const TimTexture& t = m_Textures[tex];
            su.uv[0] = t.uvb[0]; su.uv[1] = t.uvb[1];
            su.uv[2] = t.uvb[2]; su.uv[3] = t.uvb[3];
        }
    }

    m_Instances.push_back(mi);
}

//======================================================================
//  検索
//======================================================================
const GeneratorBlock* EffectContext::FindGenerator(const char* name4) const
{
    if (name4 == nullptr) return nullptr;
    for (const GeneratorBlock& g : m_Generators)
        if (memcmp(g.chunk.name, name4, 4) == 0) return &g;
    return nullptr;
}

// 0x05 generator の +0xAC リンク名 → 0x20 テクスチャ番号（見つからなければ -1＝白）
int EffectContext::FindTextureIndex(const char* name4) const
{
    if (name4 == nullptr || name4[0] == '\0') return -1;
    for (size_t i = 0; i < m_TexNames.size(); ++i)
    {
        if (memcmp(m_TexNames[i].c_str(), name4, 4) == 0) return (int)i;
    }
    return -1;
}

// 0x1F の +0x1E（先頭 8 文字＝グループ名）→ 0x20 テクスチャ番号
//   実測: 0x1F 'fire' = "fire1   "+"fire16  " / 0x20 = "fire1   fire15  " → 前半 8 文字が一致
int EffectContext::FindTextureIndexByGroup(const char* name8) const
{
    if (name8 == nullptr || name8[0] == '\0') return -1;
    for (size_t i = 0; i < m_Textures.size(); ++i)
    {
        const std::string& s = m_Textures[i].name;   // 0x20 の 16 B 名（+17 から）
        if (s.size() < 8) continue;
        if (memcmp(s.c_str(), name8, 8) == 0) return (int)i;
    }
    return -1;
}

// 0x1F の +0x1E（16 文字 = グループ 8 + specific 8）→ 0x20 テクスチャ番号
//   0x20 の 16 B 名とそのまま比較する（実測: "fire1   fire16  " 等）
int EffectContext::FindTextureIndexBy16(const char* name16) const
{
    if (name16 == nullptr || name16[0] == '\0') return -1;
    for (size_t i = 0; i < m_Textures.size(); ++i)
    {
        const std::string& s = m_Textures[i].name;
        if (s.size() < 16) continue;
        if (memcmp(s.c_str(), name16, 16) == 0) return (int)i;
    }
    // 末尾空白を無視して比較
    char a[17] = {};
    memcpy(a, name16, 16);
    for (int k = 15; k >= 0 && a[k] == ' '; --k) a[k] = '\0';
    for (size_t i = 0; i < m_Textures.size(); ++i)
    {
        const std::string& s = m_Textures[i].name;
        if (s.size() >= 16 && memcmp(s.c_str(), a, 16) == 0) return (int)i;
    }
    return -1;
}

// 0x21 の specific 名（8 文字）→ 0x20 テクスチャ番号
//   0x20 の 16 B 名は「グループ 8 + specific 8」なので後半で照合する
int EffectContext::FindTextureIndexBySpecific(const char* name8) const
{
    if (name8 == nullptr || name8[0] == '\0') return -1;
    const size_t n = strlen(name8);
    for (size_t i = 0; i < m_TexSpecific.size(); ++i)
    {
        if (m_TexSpecific[i].size() == n && memcmp(m_TexSpecific[i].c_str(), name8, n) == 0)
            return (int)i;
    }
    return -1;
}

ID3D11ShaderResourceView* EffectContext::TextureSrv(int index) const
{
    if (index >= 0 && index < (int)m_Textures.size()) return m_Textures[index].srv;
    return (m_WhiteTex.srv != nullptr) ? m_WhiteTex.srv : nullptr;
}

