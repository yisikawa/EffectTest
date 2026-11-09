#pragma once
//======================================================================
//  Skeleton.h : モデルの骨（0x29）とモーション（0x2B）だけを読む層
//
//  目的（P1a）: エフェクトを「モデルの体の位置」に合わせるために必要な
//               骨のワールド行列だけを提供する。メッシュ（0x2A）は扱わない。
//
//  レイアウト（ModelTest の読込コードを参照。ModelTest は変更しない）:
//    0x29  : +0x12 = 本数(short)、+0x14 から 30 バイト/本
//            +0 parent(u8) / +1 term(u8) / +2 quat(16) / +18 translation(12)
//    0x2B  : +0 名(4) / +18 要素数(short) / +20 キー数(short) / +22 尺(float)
//            +26 に DAT2B[要素数]（84 バイト）＋ 可変長のキー配列(float)
//            DAT2B = no, idx_qtx..qtw, qtx..qtw, idx_tx..tz, tx..tz,
//                    idx_sx..sz, sx..sz
//            キー値 = (idx & 0x7fffffff) ? fdat[idx + i] : 定数
//            キー時刻 = 1/尺*100*i（％）
//======================================================================
#include "Dat.h"
#include <string>
#include <vector>

//----------------------------------------------------------------------
// 骨の定義（0x29 の 1 レコード、30 バイト）
//----------------------------------------------------------------------
struct SkelBone
{
    int   parent = -1;
    bool  term   = false;
    float q[4]   = { 0.0f, 0.0f, 0.0f, 1.0f };   // 初期回転（クォータニオン）
    float t[3]   = { 0.0f, 0.0f, 0.0f };         // 初期移動
};

//----------------------------------------------------------------------
// モーションの 1 キー（1 ボーン分）
//----------------------------------------------------------------------
struct SkelKey
{
    float time = 0.0f;                            // ％（0〜100）
    float q[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float t[3] = { 0.0f, 0.0f, 0.0f };
    float s[3] = { 1.0f, 1.0f, 1.0f };
};

//----------------------------------------------------------------------
// モーション（0x2B チャンク 1 つ）
//----------------------------------------------------------------------
struct SkelMotion
{
    char   name[5] = {};                          // 4 文字（'atk0' 等）
    float  duration = 0.0f;                       // 尺（秒）
    int    keys = 0;                              // キー数
    std::vector<std::vector<SkelKey>> perBone;    // [boneNo] -> keys
};

//----------------------------------------------------------------------
// 骨格（読込・評価）
//----------------------------------------------------------------------
class Skeleton
{
public:
    bool Load(const DatFile& dat);                // 0x29 + 0x2B を読む

    int  BoneCount() const { return (int)m_Bones.size(); }
    size_t MotionCount() const { return m_Motions.size(); }
    const char* MotionName(size_t i) const { return (i < m_Motions.size()) ? m_Motions[i].name : ""; }
    float MotionDuration(size_t i) const { return (i < m_Motions.size()) ? m_Motions[i].duration : 0.0f; }
    int   MotionKeys(size_t i) const { return (i < m_Motions.size()) ? m_Motions[i].keys : 0; }

    bool SetMotion(const char* name4);            // 名前（4 文字）で選択。空文字で解除
    int  CurrentMotion() const { return m_Current; }

    // ルート変換（ModelTest の m_mRootTransform 相当）。
    //   ModelTest は既定で matrixMirrorY = diag(1,-1,1) を全ボーンに掛ける（左手系合わせ）。
    enum RootMode
    {
        ROOT_NONE = 0,        // 変換なし（DAT の生の値）
        ROOT_MIRROR_Y = 1,    // diag(1,-1,1)    ← ModelTest の既定
        ROOT_ROT_X180 = 2,    // X 軸 180 度（右手系を保ったまま上下反転）
        ROOT_ROT_Z180 = 3,    // Z 軸 180 度
    };
    void SetRootMode(int mode) { m_RootMode = mode; Update(m_Time); }
    int  RootMode() const { return m_RootMode; }
    const char* RootModeName() const;

    void Update(float timeSec);                   // モーション適用 → ワールド行列

    const float* Local(int bone) const;           // 16 floats（行優先）
    const float* World(int bone) const;           // 16 floats（行優先）

    // 骨の原点（ワールド）を書き出す（検証用）
    void BoneOrigins(std::vector<float>& xyz) const;

private:
    void EvalBone(int i, float pct);

    std::vector<SkelBone>   m_Bones;
    std::vector<SkelMotion> m_Motions;
    std::vector<float>      m_Local;              // [bone*16]
    std::vector<float>      m_World;              // [bone*16]
    int                     m_Current = -1;
    float                   m_Time    = 0.0f;
    int                     m_RootMode = ROOT_MIRROR_Y;   // 既定 = ModelTest の matrixMirrorY
};
