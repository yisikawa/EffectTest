#include "Effect.h"
#include <cmath>

//======================================================================
//  MotionInstance
//======================================================================
void MotionInstance::Reset(const DatFile* d, const GeneratorBlock* g, float start, int tex, int op)
{
    dat       = d;
    gen       = g;
    startTime = start;
    expired   = false;
    texIndex  = tex;
    spawnOp   = op;

    sub.Clear();
    sub.Elapsed() = 0.0f;
    sub.T01()     = 0.0f;
    float* prev = sub.At(SUB_PREV);
    prev[0] = prev[1] = prev[2] = 0.0f;
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
//  0x07 ブロックの命令列開始オフセットを自動判定する
//   エフェクトの "main" は +0x80、NPC の 0x07（atk0 等）は +0x50 から始まる。
//   候補ごとに命令列を走査し「正常終端し、命令数が最も多い」ものを採用する。
//======================================================================
static uint32_t DetectProgramStart(const DatFile& dat, const DatChunk& c)
{
    static const uint32_t cand[] = { 0x80, 0x50, 0x40, 0x60, 0x70, 0x90, 0xA0 };

    uint32_t best = 0;
    int bestScore = -1;

    for (uint32_t off : cand)
    {
        if (off >= c.size) continue;

        uint32_t pos = c.offset + off;
        const uint32_t end = c.End();
        int n = 0;
        bool ok = false;

        while (pos + 8 <= end && n < 1024)
        {
            const uint8_t* p = dat.At(pos);
            if (p == nullptr) break;

            const uint8_t op  = p[0];
            const uint8_t len = p[1] & 0x1F;

            if (op == 0x00) { ok = (n > 0); break; }
            if (len == 0)   { ok = (n > 0); break; }

            ++n;
            pos += (uint32_t)len * 4;
        }
        if (pos == end) ok = (n > 0);            // チャンク末尾まで到達

        if (ok && n > bestScore) { bestScore = n; best = off; }
    }
    return best;
}

bool EffectContext::BuildInternal(const DatFile& dat, const char* mainChunkName)
{
    m_Dat = &dat;
    m_Timeline.clear();
    m_Generators.clear();
    m_Textures.clear();
    m_Instances.clear();
    m_Duration = 0.0f;

    //--------------------------------------------------------------
    // 1) ジェネレータ（type 0x05）索引
    //--------------------------------------------------------------
    for (const DatChunk& c : dat.Chunks())
    {
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

        uint32_t o = 0;
        memcpy(&o, p + 0x80, 4); g.tblA = g.payloadOff + (o - 0x90);
        memcpy(&o, p + 0x84, 4); g.tblB = g.payloadOff + (o - 0x90);
        memcpy(&o, p + 0x88, 4); g.tblC = g.payloadOff + (o - 0x90);
        memcpy(&o, p + 0x8C, 4); g.tblD = g.payloadOff + (o - 0x90);

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
        // エフェクトの "main" は +0x80 が実測済みの正解。
        // それ以外（NPC の 0x07 アクション等）は自動判定する。
        uint32_t progStart;
        if (memcmp(mainChunk->name, "main", 4) == 0)
        {
            progStart = 0x80;
        }
        else
        {
            progStart = DetectProgramStart(dat, *mainChunk);
            if (progStart == 0) progStart = 0x80;              // フォールバック
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
        if (CreateTimTexture(device, m_Dat->At(c.offset), c.size, t))
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
    case 0x03:      // 別種生成（本ビューアでは同じ経路で生成）
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

void EffectContext::Spawn(const char* name4, float startSec, int op)
{
    const GeneratorBlock* g = FindGenerator(name4);
    if (g == nullptr) return;

    MotionInstance mi;
    mi.Reset(m_Dat, g, startSec, m_Textures.empty() ? -1 : 0, op);

    // 直近の色（op 0x2A）を初期色として与える
    float* col = mi.sub.Color();
    col[0] = m_LastColor[0] / 255.0f;
    col[1] = m_LastColor[1] / 255.0f;
    col[2] = m_LastColor[2] / 255.0f;
    col[3] = m_LastColor[3] / 255.0f;

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

ID3D11ShaderResourceView* EffectContext::TextureSrv(int index) const
{
    if (index >= 0 && index < (int)m_Textures.size()) return m_Textures[index].srv;
    return (m_WhiteTex.srv != nullptr) ? m_WhiteTex.srv : nullptr;
}

