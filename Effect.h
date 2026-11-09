#pragma once
//======================================================================
//  Effect.h : エフェクト DAT モデル（タイムライン / ジェネレータ / サブオブジェクト）
//
//  AltanaView 0.6.2 の解析結果に基づく実装。
//    timeline (type 0x07)    : 命令列（+0x50 起点、len==0 で終端）
//    generator (type 0x05)   : 0x90 バイトヘッダ + ペイロード（VM プログラム）
//    sub object (0x108 + N)  : VM の出力先（pos/scale/color/経過時間/差分）
//
//  サブオブジェクトは「フラットな float 配列」で保持する。
//  AltanaView は dword インデックスで同じ領域を使い回しており
//  （VM#2 の op 別アキュムレータが pos/scale と重なる）、
//  バイト配置を再現しないと挙動が一致しないため。
//======================================================================
#include "Dat.h"
#include "Tim.h"
#include <d3d11.h>
#include <vector>

//----------------------------------------------------------------------
// サブオブジェクト内のバイトオフセット（AltanaView 実測値）
//----------------------------------------------------------------------
enum : uint32_t
{
    SUB_MATRIX  = 0x000,   // float[16] 4x4 行列
    SUB_ID      = 0x040,   // u32
    SUB_POS     = 0x080,   // float[3] 位置          (param_2[0x20..0x22])
    SUB_MOVE    = 0x08C,   // float[3] 移動/回転目標 (param_2[0x23..0x25])
    SUB_SCALE   = 0x098,   // float[3] スケール      (param_2[0x26..0x28])
    SUB_COLOR   = 0x0A4,   // float[4] 色            (param_2[0x29..0x2b])
    SUB_ELAPSED = 0x0EC,   // float 経過秒           (param_2[0x3b])
    SUB_T01     = 0x0F0,   // float 正規化時間 0..1  (param_2[0x3c])
    SUB_PREV    = 0x0F4,   // float[3] 前フレーム位置(param_2[0x3d..0x3f])
    SUB_ROOT    = 0x100,   // u32 ルート逆参照       (param_2[0x40])
    SUB_ID2     = 0x104,   // u32                    (param_2[0x41])
    SUB_DEST    = 0x108,   // float[N] VM#1 出力スロット（vtable の dest インデックス）
    SUB_DEST_N  = 64,      // 出力スロット数（十分な余裕）
};

//----------------------------------------------------------------------
// サブオブジェクト（0x108 + dest*4 バイト）
//----------------------------------------------------------------------
struct SubObject
{
    float f[(SUB_DEST + SUB_DEST_N * 4) / 4];

    void Clear()
    {
        memset(f, 0, sizeof(f));
        for (int i = 0; i < 4; ++i) f[(SUB_MATRIX / 4) + i * 5] = 1.0f;   // 対角 1
        f[SUB_COLOR / 4 + 0] = 1.0f;
        f[SUB_COLOR / 4 + 1] = 1.0f;
        f[SUB_COLOR / 4 + 2] = 1.0f;
        f[SUB_COLOR / 4 + 3] = 1.0f;
        f[SUB_SCALE / 4 + 0] = 1.0f;
        f[SUB_SCALE / 4 + 1] = 1.0f;
        f[SUB_SCALE / 4 + 2] = 1.0f;
    }
    float*       At(uint32_t byteOff)       { return &f[byteOff / 4]; }
    const float* At(uint32_t byteOff) const { return &f[byteOff / 4]; }
    float*       Pos()   { return At(SUB_POS); }
    float*       Move()  { return At(SUB_MOVE); }
    float*       Scale() { return At(SUB_SCALE); }
    float*       Color() { return At(SUB_COLOR); }
    float*       Dest(uint32_t i) { return At(SUB_DEST + i * 4); }
    float&       Elapsed() { return f[SUB_ELAPSED / 4]; }
    float&       T01()     { return f[SUB_T01 / 4]; }
    const float& Elapsed() const { return f[SUB_ELAPSED / 4]; }
    const float& T01()     const { return f[SUB_T01 / 4]; }
};

//----------------------------------------------------------------------
// タイムライン命令（main ブロック）
//----------------------------------------------------------------------
struct TimelineInst
{
    uint8_t  op;        // +0x00
    uint8_t  len;       // +0x01 命令長 = (len & 0x1F) dwords
    uint16_t delay;     // +0x04 消費フレーム数
    uint16_t param;     // +0x06
    char     name[5];   // +0x08（4 文字 + NUL）
    uint8_t  rgba[4];   // +0x08（op 0x2A のとき）
    uint8_t  extra;     // +0x1E
    uint32_t offset;    // main チャンク内オフセット（デバッグ用）
};

//----------------------------------------------------------------------
// ジェネレータ（type 0x05）
//----------------------------------------------------------------------
struct GeneratorBlock
{
    DatChunk chunk;
    uint32_t payloadOff  = 0;   // chunk.offset + 0x90
    uint32_t payloadSize = 0;
    uint32_t tblA = 0, tblB = 0, tblC = 0, tblD = 0;   // 絶対ファイルオフセット
    uint32_t id       = 0;      // +0x10
    uint16_t frames   = 0;      // +0x76
    uint8_t  count    = 0;      // +0x78
    uint8_t  flag     = 0;      // +0x79
    char     linkName[5] = {};  // +0xAC の 4 文字リンク名（0x20/0x1F/0x21/0x3D/base/予約語）

    inline float Length() const { return (frames + 1) * (1.0f / 60.0f); }
};

//----------------------------------------------------------------------
// 実行中のモーションインスタンス
//----------------------------------------------------------------------
struct MotionInstance
{
    const GeneratorBlock* gen      = nullptr;
    const DatFile*        dat      = nullptr;
    SubObject             sub;
    float                 startTime = 0.0f;
    bool                  expired   = false;
    int                   texIndex  = -1;
    int                   spawnOp   = 2;

    void Reset(const DatFile* d, const GeneratorBlock* g, float start, int tex, int op);
    void Eval(const class EffectContext& ctx, float dt);
};

//----------------------------------------------------------------------
// エフェクトコンテキスト（AltanaView の EffectRoot 相当）
//----------------------------------------------------------------------
class EffectContext
{
public:
    EffectContext() = default;
    ~EffectContext() { ReleaseTextures(); }

    // 構築
    bool Build(const DatFile& dat);              // main / generator / テクスチャ索引
    bool BuildFromChunk(const DatFile& dat, const char* chunk4);   // 任意の 0x07 ブロックを
                                                                   // タイムラインとして構築
    bool BuildTextures(ID3D11Device* device);    // 0x20 チャンクを SRV 化
    void ReleaseTextures();

    void Reset();                                // 再生位置を先頭へ
    void Advance(float dt);                      // 1 フレーム進める

    // 参照
    const DatFile* Dat() const { return m_Dat; }
    const std::vector<TimelineInst>&   Timeline()   const { return m_Timeline; }
    const std::vector<GeneratorBlock>& Generators() const { return m_Generators; }
    const std::vector<MotionInstance>& Instances()  const { return m_Instances; }
    const std::vector<TimTexture>&     Textures()   const { return m_Textures; }

    size_t   Ip()       const { return m_Ip; }
    float    TimeLeft() const { return m_TimeLeft; }
    float    Duration() const { return m_Duration; }
    uint32_t Frame()    const { return m_Frame; }
    bool     Finished() const { return m_Finished; }
    bool     Loop()     const { return m_Loop; }
    void     SetLoop(bool v) { m_Loop = v; }
    float    Speed()    const { return m_Speed; }
    void     SetSpeed(float v) { m_Speed = v; }
    const uint8_t* LastColor() const { return m_LastColor; }

    const GeneratorBlock* FindGenerator(const char* name4) const;
    int FindTextureIndex(const char* name4) const;   // +0xAC のリンク名 → 0x20 の番号（無ければ -1）
    ID3D11ShaderResourceView* TextureSrv(int index) const;

private:
    void ExecInst(const TimelineInst& in);
    void Spawn(const char* name4, float startSec, int op);
    bool BuildInternal(const DatFile& dat, const char* mainChunkName);

    const DatFile*              m_Dat = nullptr;
    std::vector<TimelineInst>   m_Timeline;
    std::vector<GeneratorBlock> m_Generators;
    std::vector<MotionInstance> m_Instances;
    std::vector<TimTexture>     m_Textures;
    std::vector<std::string>    m_TexNames;   // 0x20 チャンク名（チャンク順。texIndex の基準）

    size_t   m_Ip       = 0;
    float    m_TimeLeft = 0.0f;
    float    m_Duration = 0.0f;
    uint32_t m_Frame    = 0;
    bool     m_Finished = false;
    bool     m_Loop     = true;
    float    m_Speed    = 1.0f;
    uint8_t  m_LastColor[4] = { 255, 255, 255, 255 };

    TimTexture m_WhiteTex;
};

