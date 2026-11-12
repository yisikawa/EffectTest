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
    SUB_FLOAT_N = (SUB_DEST + SUB_DEST_N * 4) / 4,   // f[] の要素数（= 130）
};

//----------------------------------------------------------------------
// サブオブジェクト（0x108 + dest*4 バイト）
//----------------------------------------------------------------------
struct SubObject
{
    // VM が dest に書くのは最大 3 float（色 0x16 は Color() を直接使う）。
    // それでも境界判定は `dest + 4 <= SUB_DEST_N` で行う（4 成分書込みが
    // 将来入っても溢れないため）。有効な dest は 0..60。
    float f[SUB_FLOAT_N];

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

// f[] の大きさと出力スロット数の整合（問題 5 の再発防止）
//   dest の判定は `dest + 4 <= SUB_DEST_N`（= dest <= SUB_DEST_N - 4）。
//   その最大 dest に 3 float を書いても f[] の末尾を越えないことを保証する。
static_assert(SUB_FLOAT_N * 4 == SUB_DEST + SUB_DEST_N * 4,
              "SubObject::f のバイト数と SUB_DEST_N が不整合");
static_assert(SUB_FLOAT_N >= (SUB_DEST / 4) + (SUB_DEST_N - 4) + 3,
              "dest の上限で書く 3 float が f[] に収まらない");

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
// テクスチャフレーム（type 0x21）
//
//  0x21 ブロック = ヘッダ + N フレーム。各フレームは 6 頂点 × 24 B の
//  クワッド（x, y, z, color(BGRA), u, v）を 1 個以上持つ。
//  ここでは UV 矩形とローカル座標の範囲だけを保持する。
//----------------------------------------------------------------------
struct TexFrame
{
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;   // 正規化 UV 矩形
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;   // ローカル座標の範囲（参考）
    float cr = 1.0f, cg = 1.0f, cb = 1.0f;              // 頂点色の平均（0x80 = 1.0）
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
    char     texName[9]  = {};  // 同名 0x21 の +0x20（specific 名 8 文字）。テクスチャ解決に使う

    // データ2 の op 0x01（48 B）: bil（0=平面 / 1=ビルボード）、+0x0C に 4 文字名
    int      bil = 1;           // 既定はビルボード
    int      partId = -1;       // op 0x01 の +0x06（アタッチ部位 ID。0x001/0x081 …）
    int      lifeFrames = 0;    // op 0x01 の +0x22（LifeTime。60 で 1 秒）
    bool     hasPartOfs = false;                  // op 0x01 の位置オフセットが有効か
    char     partName[5] = {};  // op 0x01 の +0x0C（テクスチャ名 4 文字）
    float    partOfs[3] = { 0.0f, 0.0f, 0.0f };   // op 0x01 の +0x10/+0x14/+0x18
    std::vector<TexFrame> texFrames;   // 同名の 0x21 があればそのフレーム列（無ければ空）

    // 0x1F（ジオメトリ）: 頂点は half-float(16bit) 位置。寸法と UV を bbox で持つ
    bool     hasGeo     = false;
    bool     geoFlat    = false;  // XZ 平面（地面デカール）か
    float    geoHalf[2]   = { 0.0f, 0.0f };
    float    geoCenter[2] = { 0.0f, 0.0f };
    float    geoUV[4]     = { 0.0f, 0.0f, 1.0f, 1.0f };

    // 6 頂点（=2 三角形）ごとのサブクワッド。実頂点の 3D 形状をそのまま保持する
    //   A=v0, B=v1, C=v2 の 3 頂点から平行四辺形を作る（A' = B + C - A が対角）
    struct GeoQuad
    {
        float ctr[3] = { 0.0f, 0.0f, 0.0f };   // 中心
        float aR[3]  = { 0.0f, 0.0f, 0.0f };   // 中心→B 方向の半分
        float aU[3]  = { 0.0f, 0.0f, 0.0f };   // 中心→C 方向の半分
        float uv[4]  = { 0.0f, 0.0f, 1.0f, 1.0f };
    };
    std::vector<GeoQuad> geoQuads;
    char     geoName[9]   = {};   // 0x1F +0x1E の 8 文字（グループ名）
    char     geoName16[17] = {};  // 0x1F +0x1E の 16 文字（= 0x20 の 16 バイト名と一致）

    inline bool HasFrames() const { return !texFrames.empty(); }

    inline float Length() const { return (frames + 1) * (1.0f / 60.0f); }
};

//----------------------------------------------------------------------
// 粒子 1 個ぶんの状態（AltanaView のサブオブジェクト 1 個に相当）
//   1 generator = `+0x78 + 1` 個のサブを持つ（stride = 0x108 + count*4）
//----------------------------------------------------------------------
struct SubUnit
{
    SubObject sub;                                        // VM の出力先（0x108 + N）
    float     uv[4]         = { 0.0f, 0.0f, 1.0f, 1.0f }; // 0x21 のフレーム矩形
    int       frameIdx      = 0;                          // 現在のフレーム番号
    float     quadHalf[2]   = { 0.0f, 0.0f };             // 0x21 クワッドの半幅・半高
    float     quadCenter[2] = { 0.0f, 0.0f };             // 同・中心オフセット
    // キーフレーム表の参照（VM#1 の 0x21..0x2F / 0x33..0x36 / 0x59..0x5B が設定）
    //   AltanaView の particle[dest+0x43] に相当。
    //   **命令の第 3 dword はオフセットではなく 0x19 チャンク名**（"k006" 等）なので、
    //   スポーン時（VM#1）に名前解決し「表の先頭オフセット + 件数」で保持する。
    //   keyCount == 0 は未解決。VM#2 はその場合スロットを書かない（0 で上書きしない）。
    uint32_t  keyFirst[SUB_DEST_N] = {};   // 0x19 のチャンク先頭 + 0x18（[time,value] の先頭）
    uint8_t   keyCount[SUB_DEST_N] = {};   // エントリ数（0..64）
    uint8_t   keyCur[SUB_DEST_N]   = {};   // 補間カーソル（元実装 particle[dest+0x43] の前進位置）
};

//----------------------------------------------------------------------
// 実行中のモーションインスタンス
//----------------------------------------------------------------------
struct MotionInstance
{
    const GeneratorBlock* gen      = nullptr;
    const DatFile*        dat      = nullptr;
    std::vector<SubUnit>  subs;                            // 粒子列（サイズ = gen->count + 1）
    float                 startTime = 0.0f;
    bool                  expired   = false;
    int                   texIndex  = -1;
    ID3D11ShaderResourceView* srv   = nullptr;   // 借用（ローカル 0x20 または base の 0x20）
    int                   spawnOp   = 2;
    float                 blackRatio = 0.0f;     // テクスチャの黒割合（加算合成の自動判定用）

    //--- 連続放出（AltanaView FUN_0040e0b0 / FUN_00410090 の移植）---------
    //   AltanaView は寿命 ctx+0x1c8 の粒子を「間隔ごとに 1 個ずつ」生成し、
    //   寿命が来たら再初期化（ループ）または削除する。当方も同じ形にする。
    int   emitTotal    = 0;        // 総放出数（= gen->count + 1）
    int   emitSpawned  = 0;        // 放出済み数
    float emitTimer    = 0.0f;     // 次の放出までの残り時間
    float emitInterval = 0.0f;     // 放出間隔（= 寿命 / 総放出数）

    int SubCount() const { return (int)subs.size(); }
    SubObject& Sub0() { return subs[0].sub; }

    void Reset(const DatFile* d, const GeneratorBlock* g, float start, int tex, int op, bool applyGeo = false);
    void Eval(const class EffectContext& ctx, float dt);

    void InitSub(SubUnit& su, bool applyGeo);
    // 粒子を 1 個生成し、VM#1（＝生成時初期化）を 1 回だけ実行する（EffectVm.cpp）
    void SpawnSub(const class EffectContext& ctx, bool applyGeo);
    // 寿命到達時の再初期化（AltanaView FUN_00410090, EffectVm.cpp）
    void RespawnSub(float carryDt);
};

// 連続放出モデル（--emit）の切替。Reset が参照するためグローバルに置く
void EffectSetEmitMode(bool on);
bool EffectEmitMode();
void EffectSetEmitRate(int n);

// 0x06 / 0x1F の初期配置を「円周上（楕円）」にする（--ring）。既定 on
//   ringscale は円周半径の倍率（既定 1.0 = データ値のまま）
void EffectSetRing(bool on);
bool EffectRing();
void EffectSetRingScale(float s);
// 0x1F の半径に arg7（|−0.4..−0.8|）を使う（--ringradius dist）
void EffectSetRingDist(bool on);
bool EffectRingDist();
// キー補間の方式（true = 実測どおりの増分補間 / false = 従来の絶対補間）
void EffectSetKeyIncr(bool on);
bool EffectKeyIncr();
// 配置の形（0=line / 1=ring（均等） / 2=spiral（らせん））と円周を張る面
void EffectSetArrange(int mode);
int  EffectArrange();
void EffectSetRingPlane(int plane);
int  EffectRingPlane();
void EffectSetSpiralTurns(float t);

// 実行された VM op の集計（--opstat）。vm=1: VM#1 / vm=2: VM#2
struct OpCount { int op; int count; };
void EffectOpStatReset();
std::vector<OpCount> EffectOpStat(int vm);

// VM#1 のキー表参照（0x19 名）の解決監査（--selftest / 問題 1 の検証）
//   ops      : VM#1 のキー op の総数
//   resolved : 0x19 チャンク名として解決できた数
//   unresolved: 解決できなかった数（== ops - resolved）
struct KeyAudit { int ops = 0; int resolved = 0; int unresolved = 0; };
KeyAudit EffectKeyAudit(const DatFile& dat, const std::vector<GeneratorBlock>& gens);

// Effect.cpp からログを出すための窓口（実体は App.cpp の Log() へ転送）
void EffectLogLine(const char* fmt, ...);

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

    // 0x21（テクスチャフレーム）の UV 適用（既定 ON。--uv full で従来動作）
    void SetUseTexFrames(bool on) { m_UseTexFrames = on; }
    bool UseTexFrames() const { return m_UseTexFrames; }

    // 0x21 フレームの選び方（AltanaView は毎描画で乱数 1 枚 → 既定 random）
    void SetRandomFrame(bool on) { m_RandomFrame = on; }
    bool RandomFrame() const { return m_RandomFrame; }

    // サブ再初期化（リスポーン）フラグ: main のストリーム D に op 0x05 があると立つ
    bool LoopRespawn() const { return m_LoopRespawn; }

    // 0x21 を持たないジェネレータの UV をテクスチャの不透明領域にする（--uvcrop on）
    void SetUvCrop(bool on) { m_UvCrop = on; }
    bool UvCrop() const { return m_UvCrop; }

    // モデル従属モード（AltanaView の ctx+0x1a0 != 0 相当）
    void SetAttached(bool on) { m_Attached = on; }
    bool Attached() const { return m_Attached; }

    // 0x1F（ジオメトリ）の bbox/UV をインスタンスに適用する（--geo on。既定 off）
    void SetGeo(bool on) { m_Geo = on; }
    bool Geo() const { return m_Geo; }
    void SetEmit(bool on) { m_Emit = on; }
    bool Emit() const { return m_Emit; }

    // 粒子寿命をデータ（op 0x01 の LifeTime）から取る（--life on）
    void SetLifeFromData(bool on) { m_LifeData = on; }
    bool LifeFromData() const { return m_LifeData; }

    // ビルボード寸法に 0x21 クワッドの範囲を使う（--size quad）
    void SetQuadSize(bool on) { m_QuadSize = on; }
    bool QuadSize() const { return m_QuadSize; }

    uint32_t ProgStart() const { return m_ProgStart; }
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
    int FindTextureIndex(const char* name4) const;
    int FindTextureIndexBySpecific(const char* name8) const;   // +0xAC のリンク名 → 0x20 の番号（無ければ -1）
    int FindTextureIndexBy16(const char* name16) const;        // 0x1F の 16 文字名 → 0x20 の番号
    int FindTextureIndexByGroup(const char* name8) const;      // 0x1F の先頭 8 文字（グループ）→ 0x20
    void SetBaseTexture(const class EffectBase* base) { m_Base = base; }  // base の 0x20 を借用する
    size_t SubTimelineCount() const { return m_Subs.size(); }
    // テクスチャ索引の整合検査用（--selftest）。0x20 の数と specific 名の数は一致する
    size_t TexNameCount()     const { return m_TexNames.size(); }
    size_t TexSpecificCount() const { return m_TexSpecific.size(); }
    // generator 名が解決できなかった回数（問題 6 の診断用。--selftest が出力）
    int     MissingGenerators() const { return m_MissingGenerators; }
    ID3D11ShaderResourceView* TextureSrv(int index) const;

private:
    // 入れ子 Schedule（op 0x03 が別の 0x07 を指す場合）の実行状態
    struct SubTimeline
    {
        std::string               name;
        std::vector<TimelineInst> insts;
        size_t                    ip = 0;
        float                     timeLeft = 0.0f;
    };

    void ExecInst(const TimelineInst& in);
    void ExecSub(SubTimeline& s, const TimelineInst& in);
    bool StartSubTimeline(const char* name4);
    bool ParseTimeline(const DatChunk& c, std::vector<TimelineInst>* out) const;
    void Spawn(const char* name4, float startSec, int op);
    bool BuildInternal(const DatFile& dat, const char* mainChunkName);
    // テクスチャ索引系の配列をまとめてクリアする（問題 3: クリア漏れの再発防止）
    void ClearIndex();
    std::vector<SubTimeline>    m_Subs;

    const DatFile*              m_Dat = nullptr;
    const class EffectBase*     m_Base = nullptr;   // base（ROM\0\0.DAT）のテクスチャ借用元
    std::vector<TimelineInst>   m_Timeline;
    std::vector<GeneratorBlock> m_Generators;
    std::vector<MotionInstance> m_Instances;
    std::vector<TimTexture>     m_Textures;
    std::vector<std::string>    m_TexNames;   // 0x20 チャンク名（チャンク順。texIndex の基準）
    std::vector<std::string>    m_TexSpecific; // 0x20 の 16 B 名の後半 8 文字（specific 名）

    uint32_t m_ProgStart = 0;   // 0x07 の命令列開始（+0x50 固定）
    size_t   m_Ip       = 0;
    float    m_TimeLeft = 0.0f;
    float    m_Duration = 0.0f;
    uint32_t m_Frame    = 0;
    bool     m_Finished = false;
    bool     m_Loop     = true;
    float    m_Speed    = 1.0f;
    uint8_t  m_LastColor[4] = { 255, 255, 255, 255 };
    bool     m_UseTexFrames = true;     // 0x21 の UV 適用（--uv full で無効化）
    bool     m_RandomFrame  = false;    // 0x21 のフレームを乱数選択（AltanaView 準拠は true。--uvframe random）
    bool     m_LoopRespawn  = false;    // サブ再初期化（main のストリーム D に op 0x05）
    bool     m_UvCrop       = false;    // 0x21 無しの UV をテクスチャの不透明領域に（--uvcrop on）
    bool     m_Attached     = false;    // モデル従属（ctx+0x1a0 != 0 相当。VM#1 を実行しない）
    bool     m_Geo          = false;    // 0x1F の bbox/UV を適用（--geo on）
    bool     m_Emit         = true;     // 連続放出モデル（--emit on/off）
    bool     m_LifeData     = false;    // 粒子寿命をデータから（--life on）
    bool     m_QuadSize     = false;    // 0x21 クワッド寸法で描く（--size quad）
    int      m_MissingGenerators = 0;   // Spawn で generator 名を解決できなかった回数

    TimTexture m_WhiteTex;
};

