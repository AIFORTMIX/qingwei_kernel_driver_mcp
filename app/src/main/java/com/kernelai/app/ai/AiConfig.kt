package com.kernelai.app.ai

/**
 * AI 客户端配置数据类。
 *
 * 支持 OpenAI 及所有兼容 OpenAI Chat Completions API 的服务端
 * （例如本地 Ollama、vLLM、LM Studio、Azure OpenAI 等）。
 *
 * @property baseUrl     API 根地址，不含尾部斜杠。默认 OpenAI 官方端点。
 * @property apiKey      鉴权密钥，会以 `Bearer` 方式放入 Authorization 头。
 * @property model       模型标识，例如 gpt-4、gpt-4o、deepseek-chat 等。
 * @property maxTokens   单次补全最大 token 数。<=0 表示不限制（由服务端决定）。
 * @property temperature 采样温度，0.0 – 2.0。值越低输出越确定。
 * @property systemPrompt 系统提示词，描述 AI 在内核分析场景中的角色与能力。
 */
data class AiConfig(
    val baseUrl: String = DEFAULT_BASE_URL,
    val apiKey: String = "",
    val model: String = DEFAULT_MODEL,
    val maxTokens: Int = 4096,
    val temperature: Float = 0.7f,
    val systemPrompt: String = DEFAULT_SYSTEM_PROMPT
) {
    /** 拼接出完整的 chat completions 端点 URL。 */
    val chatCompletionsUrl: String
        get() = "${baseUrl.trimEnd('/')}/chat/completions"

    /** 快速校验配置是否可用（至少需要 apiKey）。 */
    val isValid: Boolean
        get() = apiKey.isNotBlank() && baseUrl.isNotBlank() && model.isNotBlank()

    companion object {
        const val DEFAULT_BASE_URL = "https://api.openai.com/v1"
        const val DEFAULT_MODEL = "gpt-4"

        val DEFAULT_SYSTEM_PROMPT = """
            |你是轻微 MCP 的内核分析助手，运行在一台已 root 的 Android 设备上。
            |你可以通过 MCP 工具直接访问设备内核驱动，执行以下操作：
            |
            |1. **进程分析** — 列出进程、查看进程详情、加载模块、线程列表
            |2. **内存操作** — 读取/写入/搜索目标进程内存，查看完整 VMA 映射
            |3. **断点调试** — 设置硬件断点（执行/读/写/读写）和软件断点，查看寄存器快照
            |4. **反汇编**   — 读取目标地址的原始字节，辅助反汇编分析
            |5. **调用栈**   — 在断点命中时捕获 ARM64 FP 链调用栈
            |
            |工作规范：
            |- 在执行任何内存写入或断点操作之前，先向用户确认意图与风险。
            |- 对地址、PID 等参数使用十六进制格式时要清晰标注。
            |- 分析结果要准确、简洁，必要时给出进一步建议。
            |- 如果工具返回错误，解释可能的原因并给出排查步骤。
            |- 用中文回答用户问题，技术术语保留英文原文。
        """.trimMargin()
    }
}
