// 哈迪斯2 用到而暗黑2 没用到的 NVN 接口。由 hle_nvn.c 末尾 #include，沿用它的静态记录函数与对象布局。
// NVN entry points Hades II uses and Diablo II does not. #included at the end of hle_nvn.c so it shares its static record helpers and object layouts.
// 约定：只改状态的接口照记；宿主没有对应能力又不影响画面正确性的（调试标签、检查点）直接忽略；会改画面的未支持状态只记一行日志。
// Convention: state setters are recorded as usual; calls the host has no counterpart for that cannot change the picture (debug labels, checkpoints) are ignored; unsupported state that can change the picture only logs one line.

#define HADES_ONCE(...) do { static _Atomic int once_; if (!atomic_exchange(&once_, 1)) LOG("ANDROID_NVN_HADES " __VA_ARGS__); } while (0)

// 队列内唯一一条按序执行的命令流，围栏必然先于此后的命令；等待同步对象不用再阻塞。shortcut: 只有一个队列，Hades 若建第二个队列(异步计算)时改成真等待。
// shortcut: one in-order stream, a fence is always ahead of later commands, so the wait does not block; make it a real wait if Hades creates a second queue (async compute).
NVN(nvnQueueWaitSync, NVNboolean, NVNqueue *q, const NVNsync *s) {
    (void)q; if (!s || ((const sync_t *)s)->magic != MAGIC('S', 'Y', 'N', 'C')) die("QueueWaitSync invalid sync");
    return 1;
}
NVN(nvnCommandBufferDrawElements, void, NVNcommandBuffer *c, NVNdrawPrimitive pr, NVNindexType it, int n, NVNbufferAddress ib) {
    *REC(c, OP_DRAW_IDX, p_draw_idx) = (p_draw_idx){pr, it, n, 0, 0, 1, ib};
}
NVN(nvnCommandBufferSetViewport, void, NVNcommandBuffer *c, int x, int y, int w, int h) {
    const float v[4] = {(float)x, (float)y, (float)w, (float)h};
    h_nvnCommandBufferSetViewports(c, 0, 1, v);
}
NVN(nvnCommandBufferSetDepthRanges, void, NVNcommandBuffer *c, int first, int n, const float *v) {
    if (n <= 0) return;
    if (first != 0 || n != 1 || !v) die("Android multiple depth ranges require viewport-array support");
    h_nvnCommandBufferSetDepthRange(c, v[0], v[1]);
}
NVN(nvnCommandBufferSetSampleMask, void, NVNcommandBuffer *c, int mask) { (void)c; if (mask != -1) HADES_ONCE("SetSampleMask %#x ignored", mask); }
NVN(nvnCommandBufferSetPrimitiveRestart, void, NVNcommandBuffer *c, uint32_t enable, int index) {
    (void)c; if (B(enable)) HADES_ONCE("SetPrimitiveRestart index=%#x ignored", index);
}
NVN(nvnCommandBufferSetPointSize, void, NVNcommandBuffer *c, float s) { (void)c; if (s != 1.0f) HADES_ONCE("SetPointSize %g ignored", s); }
NVN(nvnCommandBufferSetLineWidth, void, NVNcommandBuffer *c, float w) { (void)c; if (w != 1.0f) HADES_ONCE("SetLineWidth %g ignored", w); }
NVN(nvnCommandBufferSetRenderEnable, void, NVNcommandBuffer *c, uint32_t enable) { (void)c; if (!B(enable)) die("Android disabled render enable (conditional rendering) requires implementation"); }
// 整数清除：整数颜色目标清成同值的浮点位型不对，但 0 清除两者一致；非 0 时记一行
// Integer clears: a float clear is not bit-identical for integer targets, but clearing to 0 is; non-zero logs one line
NVN(nvnCommandBufferClearColori, void, NVNcommandBuffer *c, int index, const int *v, int mask) {
    p_clear_color *r = REC(c, OP_CLEAR_COLOR, p_clear_color); r->index = index; r->mask = mask;
    for (int i = 0; i < 4; i++) { r->rgba[i] = (float)v[i]; if (v[i]) HADES_ONCE("ClearColori non-zero value converted to float"); }
}
NVN(nvnCommandBufferClearColorui, void, NVNcommandBuffer *c, int index, const uint32_t *v, int mask) {
    p_clear_color *r = REC(c, OP_CLEAR_COLOR, p_clear_color); r->index = index; r->mask = mask;
    for (int i = 0; i < 4; i++) { r->rgba[i] = (float)v[i]; if (v[i]) HADES_ONCE("ClearColorui non-zero value converted to float"); }
}
NVN(nvnTextureBuilderSetSize2D, void, NVNtextureBuilder *b, int w, int h) { TB(b)->d.width = w; TB(b)->d.height = h; TB(b)->d.depth = 1; }
NVN(nvnTextureBuilderSetSize3D, void, NVNtextureBuilder *b, int w, int h, int d) { TB(b)->d.width = w; TB(b)->d.height = h; TB(b)->d.depth = d; }
NVN(nvnTextureBuilderSetSwizzle, void, NVNtextureBuilder *b, NVNtextureSwizzle r, NVNtextureSwizzle g, NVNtextureSwizzle bl, NVNtextureSwizzle a) {
    (void)b; if (r != NVN_TEXTURE_SWIZZLE_R || g != NVN_TEXTURE_SWIZZLE_G || bl != NVN_TEXTURE_SWIZZLE_B || a != NVN_TEXTURE_SWIZZLE_A) HADES_ONCE("TextureBuilderSetSwizzle non-identity ignored");
}
NVN(nvnTextureBuilderSetDepthStencilMode, void, NVNtextureBuilder *b, NVNtextureDepthStencilMode m) { (void)b; if (m != NVN_TEXTURE_DEPTH_STENCIL_MODE_DEPTH) HADES_ONCE("TextureBuilderSetDepthStencilMode %d ignored", (int)m); }
NVN(nvnTextureBuilderGetWidth, int, const NVNtextureBuilder *b) { return (int)TB(b)->d.width; }
NVN(nvnTextureBuilderGetHeight, int, const NVNtextureBuilder *b) { return (int)TB(b)->d.height; }
NVN(nvnTextureBuilderGetDepth, int, const NVNtextureBuilder *b) { return (int)TB(b)->d.depth; }
NVN(nvnTextureBuilderGetLevels, int, const NVNtextureBuilder *b) { return (int)TB(b)->d.levels; }
NVN(nvnTextureBuilderGetSamples, int, const NVNtextureBuilder *b) { return (int)TB(b)->d.samples; }
NVN(nvnTextureBuilderGetFormat, NVNformat, const NVNtextureBuilder *b) { return (NVNformat)TB(b)->d.format; }
NVN(nvnTextureBuilderGetTarget, NVNtextureTarget, const NVNtextureBuilder *b) { return (NVNtextureTarget)TB(b)->d.target; }
NVN(nvnSamplerBuilderSetBorderColori, void, NVNsamplerBuilder *b, const int *v) { for (int i = 0; i < 4; i++) SB(b)->border[i] = (float)v[i]; }
NVN(nvnSamplerBuilderSetBorderColorui, void, NVNsamplerBuilder *b, const uint32_t *v) { for (int i = 0; i < 4; i++) SB(b)->border[i] = (float)v[i]; }
