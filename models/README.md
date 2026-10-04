# 离线双向翻译模型

程序使用两份已经训练好的 OPUS Transformer 模型。中文候选未命中较长词句时使用中→英；翻译框支持双向自动翻译。运行时只加载本地文件，不联网、不训练用户输入。

| 方向 | 原始模型 / 作者 | 转换包 / 固定 revision | 原模型许可 |
| --- | --- | --- | --- |
| 英文 → 简体中文 | [Helsinki-NLP / opus-mt-en-zh](https://huggingface.co/Helsinki-NLP/opus-mt-en-zh)，OPUS / Helsinki-NLP contributors | [jiangzhuo9357 / opus-mt-en-zh-ct2](https://huggingface.co/jiangzhuo9357/opus-mt-en-zh-ct2)，`06fb49e2f6cb0485043ae703a4c2afddd4e700d7` | Apache-2.0 |
| 中文 → 英文 | [Helsinki-NLP / opus-mt-zh-en](https://huggingface.co/Helsinki-NLP/opus-mt-zh-en)，University of Helsinki Language Technology Research Group / OPUS contributors | [jiangzhuo9357 / opus-mt-zh-en-ct2](https://huggingface.co/jiangzhuo9357/opus-mt-zh-en-ct2)，`b9f5527123795289acc264489cbd4927801601d7` | CC-BY-4.0 |

下载文件的 SHA-256、大小见 `manifest.json`（英中）与 `zh-en-manifest.json`（中英）。完整许可分别保存在 `LICENSE.Apache-2.0.txt` 和 `LICENSE.CC-BY-4.0.txt`。转换包是原权重的 CTranslate2 int8 量化格式；本项目未进一步训练或修改这些权重。第三方转换包的许可标注如与上游不同，以本表注明的上游模型许可为依据保留授权与署名。

推理使用 SentencePiece 分词和 CTranslate2 4.4.0 CPU int8，每个推理进程最多使用两个计算线程。英中使用 `>>cmn_Hans<<` 目标前缀；中英使用原模型对应的源、目标分词器。Windows Python 3.12 与依赖随项目提供，版本锁定在 `translation/requirements.txt`。

应用侧按句切分、保留段落并限制长片段；英文输出的句子之间保留空格。软件语境中的少数英中术语有应用侧后处理，见 `translation/model.py`。后台请求可取消，候选模型只保留最新请求，最多缓存 64 项结果，不写入磁盘；闲置约 60 秒后释放推理进程。

这些是小型翻译模型，仍可能误译或遗漏细节；需要核对专名及专业内容。本项目的非商业许可不限制第三方模型的 Apache-2.0 / CC-BY-4.0 权利。
