#include "Skeleton.h"
#include <DirectXMath.h>
#include <cmath>

using namespace DirectX;

namespace
{
    // DAT2B（84 バイト = 21 dword）のフィールド位置（dword 単位）
    enum : int
    {
        F_NO = 0,
        F_IDX_Q = 1,  F_CN_Q = 5,
        F_IDX_T = 9,  F_CN_T = 12,
        F_IDX_S = 15, F_CN_S = 18,
    };

    inline int32_t RdI32(const uint8_t* p, int dwordIndex)
    {
        int32_t v = 0;
        memcpy(&v, p + dwordIndex * 4, 4);
        return v;
    }
    inline float RdF32(const uint8_t* p, int dwordIndex)
    {
        float v = 0.0f;
        memcpy(&v, p + dwordIndex * 4, 4);
        return v;
    }

    // (idx & 0x7fffffff) ? fdat[idx + k] : 定数
    //  fdat は DAT2B 配列の先頭（ModelTest と同じ基準）
    inline float KeyValue(const uint8_t* rec, const float* fdat, uint32_t fCount,
                          int idxField, int cnstField, int k)
    {
        const int32_t idx = RdI32(rec, idxField);
        if ((idx & 0x7fffffff) == 0) return RdF32(rec, cnstField);
        const uint32_t at = (uint32_t)(idx + k);
        if (at >= fCount) return RdF32(rec, cnstField);
        return fdat[at];
    }
}

//----------------------------------------------------------------------
bool Skeleton::Load(const DatFile& dat)
{
    m_Bones.clear();
    m_Motions.clear();
    m_Current = -1;
    m_Time    = 0.0f;

    //--------------------------------------------------------------
    // 1) 骨（0x29 は 1 チャンクに全ボーン、30 バイト/本）
    //--------------------------------------------------------------
    const DatChunk* bc = dat.FindByType(0x29);
    if (bc == nullptr || bc->size < 0x14 || !dat.InRange(bc->offset, bc->size)) return false;

    const uint8_t* p = dat.At(bc->offset);
    uint16_t boneCount = 0;
    memcpy(&boneCount, p + 0x12, 2);
    if (boneCount == 0) return false;
    if (0x14u + (uint32_t)boneCount * 30u > bc->size) return false;

    m_Bones.resize(boneCount);
    for (uint16_t i = 0; i < boneCount; ++i)
    {
        const uint8_t* r = p + 0x14 + (uint32_t)i * 30;
        SkelBone& b = m_Bones[i];
        b.parent = (int)r[0];
        b.term   = (r[1] != 0);
        memcpy(b.q, r + 2,  16);
        memcpy(b.t, r + 18, 12);
        if (b.parent >= (int)i) b.parent = -1;      // 壊れた親は根として扱う
    }

    //--------------------------------------------------------------
    // 2) モーション（0x2B は複数チャンク）
    //--------------------------------------------------------------
    for (const DatChunk& c : dat.Chunks())
    {
        if (c.type != 0x2B) continue;
        if (c.size < 26 || !dat.InRange(c.offset, c.size)) continue;

        const uint8_t* m = dat.At(c.offset);
        int16_t elements = 0, keys = 0;
        float   times = 0.0f;
        memcpy(&elements, m + 18, 2);
        memcpy(&keys,     m + 20, 2);
        memcpy(&times,    m + 22, 4);
        if (elements <= 0 || keys <= 0 || times <= 0.0f) continue;

        const uint32_t recBytes = (uint32_t)elements * 84u;
        if (26u + recBytes > c.size) continue;

        SkelMotion mo;
        memcpy(mo.name, m, 4);
        mo.name[4]  = '\0';
        mo.duration = times;
        mo.keys     = keys;
        mo.perBone.resize(m_Bones.size());

        const uint8_t* rec   = m + 26;
        // ModelTest と同じく fdat は「レコード配列の先頭」を指す float*。
        // DAT2B の idx はここからの float 番号（要素数×21 を超えるとキー配列）。
        const float*   fdat  = reinterpret_cast<const float*>(m + 26);
        const uint32_t fCount = (c.size - 26u) / 4u;

        for (int e = 0; e < elements; ++e)
        {
            const uint8_t* r = rec + (uint32_t)e * 84;
            const int32_t no = RdI32(r, F_NO);
            if (no < 0 || no >= (int)m_Bones.size()) continue;

            std::vector<SkelKey>& out = mo.perBone[no];
            if (out.size() < (size_t)keys) out.resize((size_t)keys);

            for (int k = 0; k < keys; ++k)
            {
                SkelKey key;
                key.time = (1.0f / times) * 100.0f * (float)k;

                key.q[0] = KeyValue(r, fdat, fCount, F_IDX_Q + 0, F_CN_Q + 0, k);
                key.q[1] = KeyValue(r, fdat, fCount, F_IDX_Q + 1, F_CN_Q + 1, k);
                key.q[2] = KeyValue(r, fdat, fCount, F_IDX_Q + 2, F_CN_Q + 2, k);
                key.q[3] = KeyValue(r, fdat, fCount, F_IDX_Q + 3, F_CN_Q + 3, k);

                key.t[0] = KeyValue(r, fdat, fCount, F_IDX_T + 0, F_CN_T + 0, k);
                key.t[1] = KeyValue(r, fdat, fCount, F_IDX_T + 1, F_CN_T + 1, k);
                key.t[2] = KeyValue(r, fdat, fCount, F_IDX_T + 2, F_CN_T + 2, k);

                key.s[0] = KeyValue(r, fdat, fCount, F_IDX_S + 0, F_CN_S + 0, k);
                key.s[1] = KeyValue(r, fdat, fCount, F_IDX_S + 1, F_CN_S + 1, k);
                key.s[2] = KeyValue(r, fdat, fCount, F_IDX_S + 2, F_CN_S + 2, k);

                out[k] = key;
            }
        }

        m_Motions.push_back(std::move(mo));
    }

    m_Local.assign(m_Bones.size() * 16, 0.0f);
    m_World.assign(m_Bones.size() * 16, 0.0f);
    Update(0.0f);
    return true;
}

//----------------------------------------------------------------------
bool Skeleton::SetMotion(const char* name4)
{
    m_Current = -1;
    if (name4 == nullptr || name4[0] == '\0') { Update(m_Time); return true; }

    // ModelTest の SetMotionName と同じ「先頭が数字なら入れ替える」癖を再現
    char want[5] = {};
    strncpy_s(want, name4, 4);
    if (want[0] >= '0' && want[0] <= '9')
    {
        if (want[1] >= '0' && want[1] <= '9')
        {
            const char tmp = want[0]; want[0] = want[2]; want[2] = tmp;
        }
        else
        {
            const char tmp = want[0]; want[0] = want[1]; want[1] = tmp;
        }
    }

    for (size_t i = 0; i < m_Motions.size(); ++i)
    {
        if (memcmp(m_Motions[i].name, want, 4) == 0) { m_Current = (int)i; break; }
    }
    Update(m_Time);
    return m_Current >= 0;
}

//----------------------------------------------------------------------
void Skeleton::Update(float timeSec)
{
    m_Time = timeSec;
    if (m_Bones.empty()) return;

    const SkelMotion* mo = (m_Current >= 0 && m_Current < (int)m_Motions.size())
                         ? &m_Motions[m_Current] : nullptr;

    float pct = 0.0f;
    if (mo != nullptr && mo->duration > 0.0f)
    {
        const float t = fmodf(timeSec, mo->duration);
        pct = (t < 0.0f) ? 0.0f : (t / mo->duration) * 100.0f;
    }

    for (int i = 0; i < (int)m_Bones.size(); ++i)
        EvalBone(i, pct);
}

//----------------------------------------------------------------------
const char* Skeleton::RootModeName() const
{
    switch (m_RootMode)
    {
    case ROOT_NONE:       return "none";
    case ROOT_MIRROR_Y:   return "mirrorY";
    case ROOT_ROT_X180:   return "rotX180";
    case ROOT_ROT_Z180:   return "rotZ180";
    default:              return "?";
    }
}

//----------------------------------------------------------------------
void Skeleton::EvalBone(int i, float pct)
{
    const SkelBone& b = m_Bones[i];

    float q[4] = { b.q[0], b.q[1], b.q[2], b.q[3] };
    float t[3] = { b.t[0], b.t[1], b.t[2] };
    float s[3] = { 1.0f, 1.0f, 1.0f };

    const SkelMotion* mo = (m_Current >= 0 && m_Current < (int)m_Motions.size())
                         ? &m_Motions[m_Current] : nullptr;
    if (mo != nullptr && i < (int)mo->perBone.size() && !mo->perBone[i].empty())
    {
        const std::vector<SkelKey>& ks = mo->perBone[i];
        size_t k0 = 0, k1 = 0;
        float  f  = 0.0f;
        if (pct <= ks.front().time)      { k0 = k1 = 0; }
        else if (pct >= ks.back().time)  { k0 = k1 = ks.size() - 1; }
        else
        {
            for (size_t k = 0; k + 1 < ks.size(); ++k)
            {
                if (ks[k].time <= pct && pct < ks[k + 1].time)
                {
                    k0 = k; k1 = k + 1;
                    const float span = ks[k1].time - ks[k0].time;
                    f = (span > 0.0f) ? (pct - ks[k0].time) / span : 0.0f;
                    break;
                }
            }
        }

        // 問題 10: クォータニオンの内積が負なら片方を反転してから補間する。
        //   反転しないと 180° を超える区間で「遠回り」の回転になる（nlerp の定石）。
        float dq = 0.0f;
        for (int c = 0; c < 4; ++c) dq += ks[k0].q[c] * ks[k1].q[c];
        const float sg = (dq < 0.0f) ? -1.0f : 1.0f;

        for (int c = 0; c < 4; ++c)
            q[c] = ks[k0].q[c] + (sg * ks[k1].q[c] - ks[k0].q[c]) * f;
        for (int c = 0; c < 3; ++c)
        {
            t[c] = ks[k0].t[c] + (ks[k1].t[c] - ks[k0].t[c]) * f;
            s[c] = ks[k0].s[c] + (ks[k1].s[c] - ks[k0].s[c]) * f;
        }

        // クォータニオンの正規化（線形補間の近似）
        const float n = sqrtf(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
        if (n > 1e-6f) { q[0] /= n; q[1] /= n; q[2] /= n; q[3] /= n; }
        else           { q[0] = q[1] = q[2] = 0.0f; q[3] = 1.0f; }
    }

    const XMMATRIX S = XMMatrixScaling(s[0], s[1], s[2]);
    const XMMATRIX R = XMMatrixRotationQuaternion(XMVectorSet(q[0], q[1], q[2], q[3]));
    const XMMATRIX T = XMMatrixTranslation(t[0], t[1], t[2]);
    const XMMATRIX Mo = S * R * T;                      // モーション差分（行ベクトル規約）

    // ModelTest の DynamicTransform と同じ合成:
    //   world = rest * motion、その後 parent の world を掛ける
    const XMMATRIX Rr = XMMatrixRotationQuaternion(XMVectorSet(b.q[0], b.q[1], b.q[2], b.q[3]));
    const XMMATRIX Tr = XMMatrixTranslation(b.t[0], b.t[1], b.t[2]);
    const XMMATRIX Rest = Rr * Tr;

    const XMMATRIX L = Rest * Mo;

    // ルート変換（ModelTest は全ボーンに matrixMirrorY を掛けるが、
    // 親の world を先に合成しているため「根に 1 回掛ける」のと等価）
    XMMATRIX Root = XMMatrixIdentity();
    switch (m_RootMode)
    {
    case ROOT_MIRROR_Y: Root = XMMatrixScaling(1.0f, -1.0f, 1.0f); break;
    case ROOT_ROT_X180: Root = XMMatrixRotationX(XM_PI);           break;
    case ROOT_ROT_Z180: Root = XMMatrixRotationZ(XM_PI);           break;
    default: break;
    }

    const XMMATRIX W = (b.parent >= 0)
                     ? L * XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(&m_World[(size_t)b.parent * 16]))
                     : L * Root;

    XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&m_Local[(size_t)i * 16]), L);
    XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&m_World[(size_t)i * 16]), W);
}

//----------------------------------------------------------------------
const float* Skeleton::Local(int bone) const
{
    if (bone < 0 || bone >= (int)m_Bones.size()) return nullptr;
    return &m_Local[(size_t)bone * 16];
}

const float* Skeleton::World(int bone) const
{
    if (bone < 0 || bone >= (int)m_Bones.size()) return nullptr;
    return &m_World[(size_t)bone * 16];
}

//----------------------------------------------------------------------
void Skeleton::BoneOrigins(std::vector<float>& xyz) const
{
    xyz.clear();
    xyz.reserve(m_Bones.size() * 3);
    for (size_t i = 0; i < m_Bones.size(); ++i)
    {
        const float* w = &m_World[i * 16];
        xyz.push_back(w[12]);   // 行優先: _41,_42,_43
        xyz.push_back(w[13]);
        xyz.push_back(w[14]);
    }
}
