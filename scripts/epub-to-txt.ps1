param(
    [Parameter(Mandatory = $true)]
    [string]$InputPath,

    [string]$OutputPath,

    [switch]$KeepFrontMatter
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    $resolved = Resolve-Path -LiteralPath $Path
    return $resolved.Path
}

function Get-TextContent {
    param([Parameter(Mandatory = $true)][System.IO.Compression.ZipArchiveEntry]$Entry)

    $stream = $Entry.Open()
    try {
        $reader = New-Object System.IO.StreamReader($stream)
        return $reader.ReadToEnd()
    }
    finally {
        if ($reader) {
            $reader.Dispose()
        }
        $stream.Dispose()
    }
}

function Get-PathDirectory {
    param([Parameter(Mandatory = $true)][string]$Path)

    $index = $Path.LastIndexOf('/')
    if ($index -lt 0) {
        return ''
    }

    return $Path.Substring(0, $index + 1)
}

function Resolve-Href {
    param(
        [Parameter(Mandatory = $true)][string]$BasePath,
        [Parameter(Mandatory = $true)][string]$Href
    )

    if ([string]::IsNullOrWhiteSpace($Href)) {
        return $Href
    }

    $cleanHref = $Href.Split('#')[0]
    if ([string]::IsNullOrWhiteSpace($cleanHref)) {
        return $cleanHref
    }

    if ($cleanHref.Contains('://')) {
        return $cleanHref
    }

    $baseUri = if ([string]::IsNullOrEmpty($BasePath)) {
        [Uri]'http://local/'
    }
    else {
        [Uri]("http://local/$BasePath")
    }

    $resolvedUri = [Uri]::new($baseUri, $cleanHref)
    return $resolvedUri.AbsolutePath.TrimStart('/')
}

function Decode-Html {
    param([Parameter(Mandatory = $true)][string]$Text)

    $decoded = [System.Net.WebUtility]::HtmlDecode($Text)
    $decoded = $decoded -replace "`r`n?", "`n"
    $decoded = $decoded -replace "[\u00A0\u2000-\u200B\u202F\u205F\u3000]", ' '
    return $decoded
}

function Convert-HtmlToText {
    param([Parameter(Mandatory = $true)][string]$Html)

    $text = $Html -replace "`r`n?", "`n"
    $text = [regex]::Replace($text, '(?is)<(script|style)[^>]*>.*?</\1>', '')
    $text = [regex]::Replace($text, '(?is)<\s*br\s*/?>', "`n")
    $text = [regex]::Replace($text, '(?is)</\s*(p|div|section|article|aside|blockquote|li|tr|table|h1|h2|h3|h4|h5|h6)\s*>', "`n`n")
    $text = [regex]::Replace($text, '(?is)</\s*(td|th)\s*>', "`t")
    $text = [regex]::Replace($text, '(?is)<li[^>]*>', "- ")
    $text = [regex]::Replace($text, '(?is)<img[^>]*alt\s*=\s*(["''])(.*?)\1[^>]*>', '[Image: $2]')
    $text = [regex]::Replace($text, '(?is)<[^>]+>', ' ')
    $text = Decode-Html $text
    $text = $text -replace "[ `t]+", ' '
    $text = $text -replace " *`n *", "`n"
    $text = $text -replace "`n{3,}", "`n`n"
    return $text.Trim()
}

function Should-SkipDocument {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][bool]$KeepFrontMatter
    )

    if ($KeepFrontMatter) {
        return $false
    }

    $leaf = [System.IO.Path]::GetFileName($Path).ToLowerInvariant()
    if ($leaf -match '^(cover|title|toc|nav|copyright|contents?)\b') {
        return $true
    }

    $sample = $Text
    if ($sample.Length -gt 1200) {
        $sample = $sample.Substring(0, 1200)
    }

    if ($sample -match '(?im)^\s*(table of contents|contents)\s*$') {
        return $true
    }

    if ($sample -match '(?i)table of contents url\s*:') {
        return $true
    }

    if ($sample -match '(?im)^\s*information\s*-\s*information\s*$') {
        return $true
    }

    return $false
}

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.Net.Primitives

$inputFullPath = Resolve-FullPath -Path $InputPath
if (-not $OutputPath) {
    $OutputPath = [System.IO.Path]::ChangeExtension($inputFullPath, '.txt')
}

$outputDirectory = Split-Path -Parent $OutputPath
if ($outputDirectory -and -not (Test-Path -LiteralPath $outputDirectory)) {
    New-Item -ItemType Directory -Path $outputDirectory | Out-Null
}

$zip = [System.IO.Compression.ZipFile]::OpenRead($inputFullPath)
try {
    $entryMap = @{}
    foreach ($entry in $zip.Entries) {
        $entryMap[$entry.FullName] = $entry
    }

    $containerEntry = $entryMap['META-INF/container.xml']
    if (-not $containerEntry) {
        throw 'EPUB is missing META-INF/container.xml.'
    }

    [xml]$containerXml = Get-TextContent $containerEntry
    $rootfile = $containerXml.container.rootfiles.rootfile | Select-Object -First 1
    if (-not $rootfile) {
        throw 'EPUB container.xml does not declare a rootfile.'
    }

    $opfPath = [string]$rootfile.'full-path'
    if (-not $entryMap.ContainsKey($opfPath)) {
        throw "EPUB rootfile not found: $opfPath"
    }

    [xml]$opfXml = Get-TextContent $entryMap[$opfPath]
    $opfDirectory = Get-PathDirectory $opfPath

    $manifest = @{}
    foreach ($item in $opfXml.package.manifest.item) {
        $id = [string]$item.id
        $href = Resolve-Href -BasePath $opfDirectory -Href ([string]$item.href)
        $manifest[$id] = [pscustomobject]@{
            Href = $href
            MediaType = [string]$item.'media-type'
        }
    }

    $sections = New-Object System.Collections.Generic.List[string]
    foreach ($itemref in $opfXml.package.spine.itemref) {
        $idref = [string]$itemref.idref
        if (-not $manifest.ContainsKey($idref)) {
            continue
        }

        $item = $manifest[$idref]
        if ($item.MediaType -notmatch '^(application/xhtml\+xml|text/html)$') {
            continue
        }

        if (-not $entryMap.ContainsKey($item.Href)) {
            continue
        }

        $html = Get-TextContent $entryMap[$item.Href]
        $text = Convert-HtmlToText $html
        if ([string]::IsNullOrWhiteSpace($text)) {
            continue
        }

        if (Should-SkipDocument -Path $item.Href -Text $text -KeepFrontMatter:$KeepFrontMatter.IsPresent) {
            continue
        }

        $sections.Add($text)
    }

    if ($sections.Count -eq 0) {
        throw 'No readable XHTML/HTML content was found in the EPUB spine.'
    }

    $combined = [string]::Join("`r`n`r`n", $sections)
    [System.IO.File]::WriteAllText($OutputPath, $combined, [System.Text.UTF8Encoding]::new($false))
}
finally {
    $zip.Dispose()
}

Write-Host "Wrote text file: $OutputPath"