#include "Effect.h"
#include "EffectBase.h"
#include <cmath>

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
//    ヘッダ: +0x14 = 名前数(u8)、+0x16 = 形状数(u16)、+0x18.. = u16 配列
//    頂点  : 0x36 + i*0x24（36 B、6 頂点で 1 クワッド）
//            +0x00 float3 位置 / +0x10 D3DCOLOR / +0x14 u / +0x18 v
//    末尾  : 4 文字名（16 B 間隔）
//======================================================================
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
    const uint32_t verts = (uint32_t)shapes * 3u;   // 三角形数 × 3
    const uint32_t need = start + verts * stride;
    if (need > c->size) return false;

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

    float x0 = 1e9f, y0 = 1e9f, z0 = 1e9f, x1 = -1e9f, y1 = -1e9f, z1 = -1e9f;
    float u0 = 1e9f, v0 = 1e9f, u1 = -1e9f, v1 = -1e9f;

    for (uint32_t i = 0; i < verts; ++i)
    {
        const uint8_t* q = p + start + i * stride;
        uint16_t hx = 0, hy = 0, hz = 0;
        memcpy(&hx, q + 0x00, 2);
        memcpy(&hy, q + 0x02, 2);
        memcpy(&hz, q + 0x04, 2);
        const float x = half2f(hx), y = half2f(hy), z = half2f(hz);

        float u = 0.0f, v = 0.0f;
        memcpy(&u, q + 0x1A, 4);
        memcpy(&v, q + 0x1E, 4);

        if (x < x0) x0 = x;
        if (y < y0) y0 = y;
        if (z < z0) z0 = z;
        if (x > x1) x1 = x;
        if (y > y1) y1 = y;
        if (z > z1) z1 = z;
        if (u < u0) u0 = u;
        if (v < v0) v0 = v;
        if (u > u1) u1 = u;
        if (v > v1) v1 = v;
    }

    const float ex = x1 - x0, ey = y1 - y0, ez = z1 - z0;

    // 妥当性ガード: 極端な値を弾く（未解析領域を頂点として読んだ場合の破綻防止）
    const bool sane = (ex >= 0.01f && ex <= 20.0f) &&
                      (ey >= -1.0f && ey <= 20.0f) &&
                      (ez >= -1.0f && ez <= 20.0f) &&
                      (x0 > -100.0f && x1 < 100.0f) &&
                      (y0 > -100.0f && y1 < 100.0f) &&
                      (z0 > -100.0f && z1 < 100.0f);
    if (!sane) return false;

    g.hasGeo = true;

    // Y がほぼ 0 で XZ に広がる場合は地面平面（デカール）として扱う
    g.geoFlat = (ey < 0.01f && (ex > 0.05f || ez > 0.05f));
    if (g.geoFlat)
    {
        g.geoHalf[0]   = ex * 0.5f;             // 幅（X）
        g.geoHalf[1]   = ez * 0.5f;             // 奥行（Z）
        g.geoCenter[0] = (x1 + x0) * 0.5f;
        g.geoCenter[1] = (z1 + z0) * 0.5f;
    }
    else
    {
        g.geoHalf[0]   = ex * 0.5f;
        g.geoHalf[1]   = ey * 0.5f;
        g.geoCenter[0] = (x1 + x0) * 0.5f;
        g.geoCenter[1] = (y1 + y0) * 0.5f;
    }
    // UV が退化している場合は全面（0,0,1,1）にフォールバック
    if (u1 - u0 > 1e-4f && v1 - v0 > 1e-4f)
    {
        g.geoUV[0] = u0; g.geoUV[1] = v0; g.geoUV[2] = u1; g.geoUV[3] = v1;
    }
    else
    {
        g.geoUV[0] = 0.0f; g.geoUV[1] = 0.0f; g.geoUV[2] = 1.0f; g.geoUV[3] = 1.0f;
    }

    // テクスチャ名（AltanaView FUN_0040b9d0 と同じ走査）
    //   u16 配列 (+0x18, shapes 個) の後を 4 バイト境界に揃え、2 バイトずつ進めて
    //   「2 バイトとも印字可能」な位置を名前の開始とし、以降 16 バイト間隔で names 個読む
    {
        const int names = (int)p[0x14];
        uint32_t q0 = 0x18u + (uint32_t)shapes * 2u;
        q0 = (q0 + 3u) & ~3u;

        uint32_t nmOff = 0;
        for (uint32_t q = q0; q + 2u <= c->size; q += 2u)
        {
            if (p[q] > 0x1F && p[q] < 0x7F && p[q + 1] > 0x1F && p[q + 1] < 0x7F)
            {
                nmOff = q;
                break;
            }
        }

        for (int i = 0; i < names && nmOff != 0; ++i)
        {
            const uint32_t o = nmOff + (uint32_t)i * 16u;
            if (o + 4u > c->size) break;
            char nm[5] = {};
            memcpy(nm, p + o, 4);
            nm[4] = '\0';
            if (nm[0] == '\0') continue;
            if (dat.Find(nm, 0x20) != nullptr || dat.Find(nm, 0x21) != nullptr)
            {
                strncpy_s(g.geoName, nm, _TRUNCATE);
                break;
            }
        }
    }
    return true;
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
    subs.assign((size_t)((n > 0) ? n : 1), SubUnit{});

    for (SubUnit& su : subs)
    {
        // 0x21 のフレーム矩形（先頭フレームで初期化。実際の適用は Eval が
        // EffectContext::UseTexFrames() を見て決める）
        su.frameIdx = 0;
        su.uv[0] = 0.0f; su.uv[1] = 0.0f; su.uv[2] = 1.0f; su.uv[3] = 1.0f;

        su.sub.Clear();
        su.sub.Elapsed() = 0.0f;
        su.sub.T01()     = 0.0f;
        float* prev = su.sub.At(SUB_PREV);
        prev[0] = prev[1] = prev[2] = 0.0f;

        // 0x1F（ジオメトリ）: bbox を寸法・UV として使う（--geo on のときのみ）
        if (applyGeo && g != nullptr && g->hasGeo)
        {
            su.quadHalf[0]   = g->geoHalf[0];
            su.quadHalf[1]   = g->geoHalf[1];
            su.quadCenter[0] = g->geoCenter[0];
            su.quadCenter[1] = g->geoCenter[1];
            su.uv[0] = g->geoUV[0]; su.uv[1] = g->geoUV[1];
            su.uv[2] = g->geoUV[2]; su.uv[3] = g->geoUV[3];
        }
    }
}

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

bool EffectContext::BuildInternal(const DatFile& dat, const char* mainChunkName)
{
    m_Dat = &dat;
    m_Timeline.clear();
    m_Generators.clear();
    m_Textures.clear();
    m_TexNames.clear();
    m_Instances.clear();
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
                memcpy(g.partName, q + 0x0C, 4);
                g.partName[4] = '\0';
                memcpy(&g.partOfs[0], q + 0x10, 4);
                memcpy(&g.partOfs[1], q + 0x14, 4);
                memcpy(&g.partOfs[2], q + 0x18, 4);
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

    // 寿命切れを除去（表示用の簡略化）
    if (!m_Instances.empty())
    {
        std::vector<MotionInstance> alive;
        alive.reserve(m_Instances.size());
        for (const MotionInstance& mi : m_Instances)
            if (!mi.expired) alive.push_back(mi);
        m_Instances.swap(alive);
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
bool EffectContext::ParseTimeline(const DatChunk& c, std::vector<TimelineInst>* out) const
{
    if (m_Dat == nullptr || out == nullptr) return false;
    out->clear();
    if (c.size <= 0x50) return false;

    const uint32_t start = c.offset + 0x50;
    const uint32_t end   = c.End();
    uint32_t pos = start;
    while (pos + 8 <= end)
    {
        const uint8_t* p = m_Dat->At(pos);
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
    if (m_Dat == nullptr || name4 == nullptr) return false;
    if (m_Subs.size() >= 16) return false;                       // 暴走防止

    for (const SubTimeline& s : m_Subs)
        if (memcmp(s.name.c_str(), name4, 4) == 0) return true;   // 起動済み

    const DatChunk* c = m_Dat->Find(name4, 0x07);
    if (c == nullptr) return false;

    SubTimeline s;
    s.name.assign(name4, 4);
    if (!ParseTimeline(*c, &s.insts)) return false;

    m_Subs.push_back(std::move(s));
    return true;
}

void EffectContext::ExecSub(SubTimeline& s, const TimelineInst& in)
{
    switch (in.op)
    {
    case 0x02:
        Spawn(in.name, in.param / 60.0f, (int)in.op);
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

void EffectContext::Spawn(const char* name4, float startSec, int op)
{
    const GeneratorBlock* g = FindGenerator(name4);
    if (g == nullptr) return;

    MotionInstance mi;
    // テクスチャ解決: 0x21 の specific 名（あれば）→ +0xAC のリンク名 の順
    int tex = -1;
    if (g->texName[0] != '\0') tex = FindTextureIndexBySpecific(g->texName);
    if (tex < 0) tex = FindTextureIndex(g->linkName);
    if (tex < 0 && g->geoName[0] != '\0') tex = FindTextureIndexBySpecific(g->geoName);
    mi.Reset(m_Dat, g, startSec, tex, op, m_Geo);

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

