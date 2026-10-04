param([Parameter(Mandatory=$true)][string]$SourceDirectory)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$sourceRoot=(Resolve-Path -LiteralPath $SourceDirectory).Path
$outputRoot=Join-Path $projectRoot 'data'
$revision=(& git -C $sourceRoot rev-parse HEAD).Trim()
if($LASTEXITCODE -ne 0){throw 'SourceDirectory must be the Qingjian checkout.'}
$knownPos='^(?:n|v|adj|adv|pron|prep|conj|int|interj|num|det|art|aux|phr|abbr|modal)\.\s+'
$reports=New-Object Collections.Generic.List[string]
foreach($direction in @('en','zh')){
    $sourceFile=Join-Path $sourceRoot ('assets\glossary\glossary-'+$direction+'.tsv')
    $entries=New-Object 'Collections.Generic.SortedDictionary[string,string]' ([StringComparer]::Ordinal)
    $counts=@{source=0;discarded=0;senses=0;words=0;phrases=0;noun=0;verb=0;adjective=0;other=0}
    foreach($line in [IO.File]::ReadLines($sourceFile,[Text.Encoding]::UTF8)){
        if(!$line -or $line.StartsWith('#')){continue}
        $counts.source++
        $fields=$line.Split([char]9)
        if($fields.Count -lt 2){$counts.discarded++;continue}
        $key=$fields[0].Trim()
        $validKey=if($direction -eq 'en'){$key.Length -le 32 -and $key -match '[\u3400-\u9fff]' -and $key -notmatch '[\x00-\x20]'}else{$key -cmatch "^[a-z][a-z0-9' .+/#_-]*$"}
        if(!$validKey){$counts.discarded++;continue}
        $seen=New-Object 'Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
        $senses=New-Object Collections.Generic.List[string]
        for($i=1;$i -lt $fields.Count;$i++){
            $sense=($fields[$i] -split '\|',2)[0].Trim()
            $clean=[regex]::Replace($sense,$knownPos,'').Trim()
            $valid=$clean.Length -gt 0 -and $clean.Length -le 320 -and $clean -notmatch '[\x00-\x1f\x7f]'
            if($direction -eq 'en'){$valid=$valid -and $clean -match '[A-Za-z]' -and $clean -notmatch '[\u3400-\u9fff]'}else{$valid=$valid -and $clean -match '[\u3400-\u9fff]'}
            if(!$valid -or !$seen.Add($clean) -or $senses.Count -ge 8){continue}
            $senses.Add($sense)
            $counts.senses++
            if($sense -match '^n\. '){$counts.noun++}elseif($sense -match '^v\. '){$counts.verb++}elseif($sense -match '^adj\. '){$counts.adjective++}else{$counts.other++}
        }
        if(!$senses.Count){$counts.discarded++;continue}
        $entries[$key]=[string]::Join("`t",$senses)
        $isPhrase=if($direction -eq 'en'){$key.Length -gt 4}else{$key.Contains(' ')}
        if($isPhrase){$counts.phrases++}else{$counts.words++}
    }
    $destination=Join-Path $outputRoot ('glossary-'+$direction+'.tsv')
    $writer=New-Object IO.StreamWriter($destination,$false,([Text.UTF8Encoding]::new($false)))
    try{$writer.NewLine="`n";$writer.WriteLine('# Filtered from Qingjian '+$revision+'; GPL-3.0-or-later; tools/import-qingjian.ps1');foreach($entry in $entries.GetEnumerator()){$writer.WriteLine($entry.Key+"`t"+$entry.Value)}}finally{$writer.Dispose()}
    $label=if($direction -eq 'en'){'中 → 英'}else{'英 → 中'}
    $reports.Add('| '+$label+' | '+$counts.source+' | '+$entries.Count+' | '+$counts.discarded+' | '+$counts.words+' | '+$counts.phrases+' | '+$counts.senses+' |')
    $reports.Add('<!-- '+$label+': noun='+$counts.noun+'; verb='+$counts.verb+'; adjective='+$counts.adjective+'; other='+$counts.other+'; source_sha256='+(Get-FileHash -LiteralPath $sourceFile -Algorithm SHA256).Hash+' -->')
}
$report=@'
# 离线词库筛选记录

来源：青简 Qingjian，https://github.com/qingjian-team/qingjian 。
导入版本：REVISION。两份派生词库继续按 GPL-3.0-or-later 发布。

| 方向 | 原始词条 | 保留词条 | 筛除词条 | 单词／短词 | 词组／长词 | 保留释义 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
ROWS

分类：中英键长不超过 4 字归为短词，其余归为词组／长词；英中键含空格归为词组，其余为单词。
这只是按形态分类，不表示词频或质量等级。词性按原始 n./v./adj. 等标签统计，不自动推断词性。
词条仍合并为每个方向一份 TSV，便于快速内存映射查找，不重复存放整套分类文件。

筛选：删除空键、空释义、控制字符、无目标语言文字的释义；去掉读音附注并对释义去重，最多保留 8 条。
中英排除含中文的未译释义；英中排除仅含英文／缩写的未译释义。保留有效专名和专业词，不声称已人工校对。
来源说明指出数据由 LLM 离线生成；此筛选不保证语义正确，个人词表可以覆盖错译。
未导入日语、西班牙语、拼音拼写表或输入引擎，因为它们不提供本程序需要的中英对照。

额外内容：data/phrases.tsv 为人工整理的日常及软件短句，data/supplements.tsv 补充常见名词形态。
data/chat-patterns.tsv 提供有限的日常表达模板；只有槽位也有完整对应译文时才生成句子。
英文翻译框优先整句／模板匹配；不能整句匹配时显示词组参考并标出未收录部分，不伪装成完整机器翻译。

复现：先克隆来源仓库，再运行 .\tools\import-qingjian.ps1 -SourceDirectory '青简克隆目录'。
运行程序只读取本地文件，不联网、不调用翻译 API、不限制次数。
'@
$report=$report.Replace('REVISION',$revision).Replace('ROWS',([string]::Join("`n",$reports)))
[IO.File]::WriteAllText((Join-Path $outputRoot 'LIBRARY.md'),$report,[Text.UTF8Encoding]::new($false))
Write-Output $reports
