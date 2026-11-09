#include "Effect.h"
#include "EffectBase.h"
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
        if (c.type == 0x20) { m_TexNames.push_back(c.name); continue; }   // texIndex の基準順
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
    const int tex = FindTextureIndex(g->linkName);   // +0xAC のリンク名から 0x20 を解決
    mi.Reset(m_Dat, g, startSec, tex, op);

    // 描画に使う SRV（借用）: ローカル 0x20 → base の 0x20 の順で解決
    if (tex >= 0 && tex < (int)m_Textures.size())
        mi.srv = m_Textures[tex].srv;
    else if (m_Base != nullptr && g->linkName[0] != '\0')
        mi.srv = m_Base->FindTexture(g->linkName);

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

ID3D11ShaderResourceView* EffectContext::TextureSrv(int index) const
{
    if (index >= 0 && index < (int)m_Textures.size()) return m_Textures[index].srv;
    return (m_WhiteTex.srv != nullptr) ? m_WhiteTex.srv : nullptr;
}

