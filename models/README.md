# 离线英文 → 简体中文模型

- 原模型：[Helsinki-NLP / opus-mt-en-zh](https://huggingface.co/Helsinki-NLP/opus-mt-en-zh)，OPUS / Helsinki-NLP contributors，Apache-2.0。
- CTranslate2 int8 转换包：[jiangzhuo9357 / opus-mt-en-zh-ct2](https://huggingface.co/jiangzhuo9357/opus-mt-en-zh-ct2)。固定 revision：`06fb49e2f6cb0485043ae703a4c2afddd4e700d7`。
- 下载文件的 SHA-256、字节数见 `manifest.json`；许可全文见 `LICENSE.Apache-2.0.txt`。未修改模型权重。
- 使用 SentencePiece 分词、目标前缀 `>>cmn_Hans<<`，CTranslate2 4.4.0 CPU int8 推理。使用内置 Windows Python 3.12 运行库，依赖锁定在 `translation/requirements.txt`。只在翻译框请求时加载；不联网，不需要账号或密钥。
- 应用侧保留段落、按句切分、限制长片段，并对软件语境中的少数术语进行后处理；逻辑见 `translation/model.py`。语言模型可能遗漏细节或选错词义，需要核对专名及专业内容。

本项目的非商业许可不限制第三方模型的 Apache-2.0 权利。
