#Requires -Version 5.1
<#
.SYNOPSIS
    One-command validation of VirtualItemViews: build, CTest, examples,
    benchmarks and the installed-package consumer smoke test, for every
    requested Qt kit and library flavour.

.DESCRIPTION
    This is the entry point a CI job or a release checklist calls (see
    docs/roadmap.md, Wave 3 / 3e). For each Qt kit x library flavour it:
      1. imports the MSVC environment from vcvars64.bat,
      2. configures and builds the `all` target,
      3. runs CTest (every test registered in that tree),
      4. runs every example with --exit-after <ms> and checks the exit code,
      5. runs the benchmark invariant checks,
      6. installs the package and builds + runs tests/install/consumer against
         the installed prefix (the "can somebody else consume it" check).
    Every step is reported; a failing step is not fatal, so one run shows all
    problems. The exit code is the number of failed steps.

    With -Asan the same kit gets a second combination built with MSVC's
    AddressSanitizer (tests + examples; benchmarks and the installed consumer are
    skipped there - see the parameter help).

.EXAMPLE
    pwsh -File scripts/validate.ps1
    pwsh -File scripts/validate.ps1 -Library Static -SkipBenchmarks
    pwsh -File scripts/validate.ps1 -QtBin D:/Qt/6.8.3/msvc2022_64/bin
    pwsh -File scripts/validate.ps1 -Asan -QtBin D:/Qt/6.11.2/msvc2022_64/bin
    pwsh -File scripts/validate.ps1 -Release -Library Static
    pwsh -File scripts/validate.ps1 -MinGW                 # GCC/Clang kits only
    pwsh -File scripts/validate.ps1 -MinGW -Library Static # ... with the installed consumer
#>
[CmdletBinding()]
param(
    # Qt kit "bin" directories. The kit root (its parent) becomes
    # CMAKE_PREFIX_PATH; the Qt major version is detected from the DLLs.
    [string[]]$QtBin = @('D:\devlib\Qt\6.11.2\msvc2022_64\bin',
                         'D:\devlib\Qt\5.15.2\msvc2019_64\bin'),
    [ValidateSet('Static', 'Shared', 'Both')][string]$Library = 'Both',
    [string]$Vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat',
    [string]$CMake,
    [int]$ExampleMs = 400,
    [switch]$SkipExamples,
    [switch]$SkipBenchmarks,
    [switch]$SkipConsumer,
    # Validates the Release configuration instead of Debug: separate trees
    # (<...>-release[ -shared]) and ctest -C Release. Debug stays the default, and the
    # two are independent runs - Release is where the optimised / NDEBUG code paths and
    # the numbers quoted in docs/performance.md §3 come from.
    [switch]$Release,
    # Adds a fifth step combination per Qt kit: the library + tests + examples built with
    # MSVC's AddressSanitizer (-DCMAKE_CXX_FLAGS=/fsanitize=address, tree <...>-asan).
    # The benchmarks would take minutes under ASan and the installed consumer is a
    # separate CMake project that would have to be given the sanitizer flags as well, so
    # both are skipped in this mode.
    [switch]$Asan,
    # Run the MinGW / llvm-mingw kits listed in -MinGWKits *in addition to* the MSVC
    # combos. They bring their own compiler (put on PATH, no vcvars) and their own
    # trees (cmake-build-mingw-<name>), which is how GCC and Clang coverage is obtained
    # on a machine without Linux.
    [switch]$MinGW,
    # UndefinedBehaviorSanitizer. MSVC has neither UBSan nor a way to mix it in, so this
    # runs the llvm-mingw (Clang) kit named by -UBSanKit with
    # -fsanitize=undefined -fno-sanitize-recover=undefined. The installed consumer is
    # skipped: it is a separate CMake project and would have to be given the flags too.
    [switch]$UBSan,
    [string]$UBSanKit = 'clang17-qt6',
    # Qt "bin" + compiler "bin" + compiler names of the MinGW kits. The defaults match
    # the Qt online installer layout used for the v1.0 verification (Qt 6.11.2 mingw and
    # llvm-mingw, Qt 5.15.2 mingw81).
    [object[]]$MinGWKits = @(
        @{ Name = 'gcc13-qt6';  QtBin = 'D:\devlib\Qt\6.11.2\mingw_64\bin';      CompilerBin = 'D:\devlib\Qt\Tools\mingw1310_64\bin';     Cxx = 'g++.exe';    Cc = 'gcc.exe' },
        @{ Name = 'clang17-qt6'; QtBin = 'D:\devlib\Qt\6.11.2\llvm-mingw_64\bin'; CompilerBin = 'D:\devlib\Qt\Tools\llvm-mingw1706_64\bin'; Cxx = 'clang++.exe'; Cc = 'clang.exe' },
        @{ Name = 'gcc81-qt5';  QtBin = 'D:\devlib\Qt\5.15.2\mingw81_64\bin';    CompilerBin = 'D:\devlib\Qt\Tools\mingw810_64\bin';      Cxx = 'g++.exe';    Cc = 'gcc.exe' }
    ),
    # Ninja bundled with the Qt installer (the MinGW kits have no ninja of their own).
    [string]$NinjaBin = 'D:\devlib\Qt\Tools\Ninja'
)

if ($Asan) {
    $SkipBenchmarks = $true
    $SkipConsumer = $true
}
if ($Asan -and $Release) {
    throw "-Asan and -Release are separate runs: pick one (Release + ASan would need its own tree and numbers)."
}
$configuration = if ($Release) { 'Release' } else { 'Debug' }
if ($UBSan) {
    $MinGW = $true
    $SkipConsumer = $true
    if (-not $PSBoundParameters.ContainsKey('QtBin')) {
        $QtBin = @()
    }
}
if ($MinGW -and -not $PSBoundParameters.ContainsKey('QtBin')) {
    # -MinGW alone means "just the MinGW kits": the MSVC combos run when the caller asks
    # for them explicitly (a -QtBin value, or no -MinGW at all).
    $QtBin = @()
}

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

$repo = Split-Path -Parent $PSScriptRoot
$results = [System.Collections.Generic.List[object]]::new()

function Add-Result
{
    param([string]$Combo, [string]$Step, [bool]$Ok, [string]$Detail = '')
    $script:results.Add([pscustomobject]@{ Combo = $Combo; Step = $Step; Ok = $Ok; Detail = $Detail })
    $mark = if ($Ok) { 'ok  ' } else { 'FAIL' }
    $suffix = if ([string]::IsNullOrWhiteSpace($Detail)) { '' } else { " -- $Detail" }
    Write-Host ("  [{0}] {1,-20} {2}{3}" -f $mark, $Step, $Combo, $suffix)
}

function Invoke-Native
{
    param([string]$File, [string[]]$Arguments)
    $output = & $File @Arguments 2>&1
    return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = @($output) }
}

function Show-Tail
{
    param($Output, [int]$Lines = 12)
    foreach ($line in @($Output | Select-Object -Last $Lines)) {
        Write-Host ("      | " + $line)
    }
}

# -- preflight ---------------------------------------------------------------

if ($QtBin.Count -gt 0 -and -not (Test-Path -LiteralPath $Vcvars -PathType Leaf)) {
    throw "vcvars64.bat not found: $Vcvars (pass -Vcvars)"
}
if (-not $CMake) {
    $fromPath = Get-Command cmake -ErrorAction SilentlyContinue
    $CMake = if ($fromPath) { $fromPath.Source } else { 'D:\CLion\bin\cmake\win\x64\bin\cmake.exe' }
}
if (-not (Test-Path -LiteralPath $CMake -PathType Leaf)) {
    throw "cmake not found: $CMake (pass -CMake)"
}
$CTest = Join-Path (Split-Path -Parent $CMake) 'ctest.exe'
if (-not (Test-Path -LiteralPath $CTest -PathType Leaf)) {
    throw "ctest.exe not found next to $CMake"
}

Write-Host "VirtualItemViews validation"
Write-Host "  repo    : $repo"
Write-Host "  cmake   : $CMake"
Write-Host "  vcvars  : $Vcvars"
Write-Host ""
if ($QtBin.Count -gt 0) {
    Write-Host "  importing the MSVC environment ..."
    foreach ($line in (& cmd.exe /c "`"$Vcvars`" >nul && set")) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path ("Env:" + $Matches[1]) -Value $Matches[2]
        }
    }
} else {
    Write-Host "  no MSVC combo requested: vcvars is not imported"
}

$examples = @('simple_list', 'order_cards', 'dynamic_height', 'million_rows',
              'table_row_widgets', 'table_many_columns', 'tree_view',
              'table_custom_header', 'drag_drop', 'table_spans', 'table_panes',
              'table_frozen_rows')

$benchmarks = @(
    [pscustomobject]@{ Name = 'list 1M rows';  Args = @('--rows', '1000000', '--steps', '200') },
    [pscustomobject]@{ Name = 'table 100 cols'; Args = @('--table', '--table-columns', '100', '--rows', '200000', '--steps', '100') },
    [pscustomobject]@{ Name = 'tree';          Args = @('--tree', '--tree-roots', '300000') }
)

# -- combinations ------------------------------------------------------------

# One kit = configure, build, CTest, examples, benchmarks, install + consumer. The
# toolchain is whatever the caller put on PATH (MSVC from vcvars, MinGW from its own
# bin) plus the extra configure arguments it passes (e.g. -DCMAKE_CXX_COMPILER=g++).
function Invoke-Kit {
    param(
        [Parameter(Mandatory)][string]$Combo,
        [Parameter(Mandatory)][string]$Tree,
        [Parameter(Mandatory)][string]$QtRoot,
        [Parameter(Mandatory)][string]$QtBin,
        [Parameter(Mandatory)][string]$Configuration,
        [string[]]$ExtraConfigure = @(),
        [string[]]$PathPrefix = @(),
        [Parameter(Mandatory)][string]$ConsumerName,
        [string]$SharedOption
    )

    Write-Host ""
    Write-Host "$Combo  ($Tree)"

    # The toolchain has to be on PATH before configure: CMake looks for ninja and for
    # the compiler while generating, not while building.
    if ($PathPrefix.Count -gt 0) {
        $env:PATH = (($PathPrefix + @($QtBin, $env:PATH)) -join ';')
    } else {
        $env:PATH = "$QtBin;$env:PATH"
    }

    # 1) configure + build
    $configureArgs = @('-S', $repo, '-B', $Tree, '-G', 'Ninja',
                       "-DCMAKE_BUILD_TYPE=$Configuration",
                       "-DCMAKE_PREFIX_PATH=$QtRoot")
    if ($SharedOption) {
        $configureArgs += "-DVIRTUALITEMVIEWS_BUILD_SHARED=$SharedOption"
    }
    $configureArgs += $ExtraConfigure
    $configure = Invoke-Native $CMake $configureArgs
    if ($configure.ExitCode -ne 0) {
        Add-Result $Combo 'configure' $false 'see output'
        Show-Tail $configure.Output
        return
    }
    Add-Result $Combo 'configure' $true

    $build = Invoke-Native $CMake @('--build', $Tree, '--target', 'all', '--config', $Configuration)
    Add-Result $Combo 'build all' ($build.ExitCode -eq 0)
    if ($build.ExitCode -ne 0) {
        Show-Tail $build.Output 25
        return
    }

    $bin = Join-Path $Tree 'bin'
    $env:PATH = "$bin;$env:PATH"
    $env:QT_QPA_PLATFORM = 'offscreen'
    # Without this Qt sends its messages to the debugger when the process has no
    # console, and the example check below would be blind to them.
    $env:QT_FORCE_STDERR_LOGGING = '1'
    if ($Asan) {
        # Qt leaves process-level allocations behind, so the leak checker has to be off:
        # this run is about use-after-free / out-of-bounds.
        $env:ASAN_OPTIONS = 'detect_leaks=0'
    }

    # 2) tests
    $test = Invoke-Native $CTest @('--test-dir', $Tree, '-C', $Configuration, '--output-on-failure')
    Add-Result $Combo 'ctest' ($test.ExitCode -eq 0)
    if ($test.ExitCode -ne 0) {
        Show-Tail $test.Output 25
    }

    # 3) examples: every one must exit 0 with --exit-after and must not report a
    #    library warning. The library's diagnostics always name the class they come
    #    from ("VirtualTableView::setHorizontalHeader(): ..."), so their names double
    #    as the deny list. QT_FATAL_WARNINGS=1 would be the direct way, but it is not
    #    usable here: Qt's offscreen plugin ("does not support propagateSizeHints")
    #    and this Qt build's missing font directory warn on their own, and the unit
    #    tests *deliberately* exercise warning paths (see docs/ci.md §3).
    if (-not $SkipExamples) {
        $libraryNames = @('VirtualItemView', 'VirtualListView', 'VirtualTableView',
                          'VirtualTreeView', 'VirtualHeaderView', 'NativeHeaderView',
                          'HeaderGeometry', 'TableSpanMap', 'TablePaneLayout',
                          'WidgetRecycler')
        $bad = @()
        foreach ($name in $examples) {
            $exe = Join-Path $bin "$name.exe"
            if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
                $bad += "$name (missing)"
                continue
            }
            $output = (& $exe --exit-after $ExampleMs 2>&1 | Out-String)
            if ($LASTEXITCODE -ne 0) {
                $bad += "$name=$LASTEXITCODE"
                continue
            }
            foreach ($libraryName in $libraryNames) {
                if ($output -like "*$libraryName*") {
                    $bad += "$name (library warning)"
                    break
                }
            }
        }
        Add-Result $Combo "examples ($($examples.Count))" ($bad.Count -eq 0) ($bad -join ', ')
    }

    # 4) benchmarks (violating a virtualization invariant is a non-zero exit)
    if (-not $SkipBenchmarks) {
        $bench = Join-Path $bin 'bench_listview.exe'
        $bad = @()
        foreach ($case in $benchmarks) {
            & $bench @($case.Args) *> $null
            if ($LASTEXITCODE -ne 0) {
                $bad += "$($case.Name)=$LASTEXITCODE"
            }
        }
        Add-Result $Combo "benchmarks ($($benchmarks.Count))" ($bad.Count -eq 0) ($bad -join ', ')
    }

    # 5) install + consumer smoke test
    if (-not $SkipConsumer) {
        $prefix = Join-Path $Tree 'install-root'
        $consumerBuild = Join-Path $repo $ConsumerName
        $install = Invoke-Native $CMake @('--install', $Tree, '--prefix', $prefix, '--config', $Configuration)
        if ($install.ExitCode -ne 0) {
            Add-Result $Combo 'install' $false
            Show-Tail $install.Output
        } else {
            Add-Result $Combo 'install' $true
            $consumerArgs = @('-S', (Join-Path $repo 'tests/install/consumer'), '-B', $consumerBuild,
                              '-G', 'Ninja', "-DCMAKE_BUILD_TYPE=$Configuration",
                              "-DCMAKE_PREFIX_PATH=$prefix;$QtRoot")
            # A MinGW kit has to hand its compiler to the consumer project too.
            foreach ($extra in $ExtraConfigure) {
                if ($extra -like '-DCMAKE_*_COMPILER=*') {
                    $consumerArgs += $extra
                }
            }
            $consumer = Invoke-Native $CMake $consumerArgs
            if ($consumer.ExitCode -ne 0) {
                Add-Result $Combo 'consumer configure' $false
                Show-Tail $consumer.Output
            } else {
                $consumerBuildStep = Invoke-Native $CMake @('--build', $consumerBuild, '--config', $Configuration)
                if ($consumerBuildStep.ExitCode -ne 0) {
                    Add-Result $Combo 'consumer build' $false
                    Show-Tail $consumerBuildStep.Output
                } else {
                    # A shared install keeps the DLL in <prefix>/bin: on Windows it has to be
                    # findable, which is exactly what an application has to arrange as well.
                    $env:PATH = "$prefix\bin;$env:PATH"
                    $run = Invoke-Native (Join-Path $consumerBuild 'viv_consumer.exe') @()
                    Add-Result $Combo 'consumer run' ($run.ExitCode -eq 0)
                    Show-Tail $run.Output 3
                }
            }
        }
    }
}

# -- MSVC kits (vcvars already imported above) -------------------------------

foreach ($qt in $QtBin) {
    if (-not (Test-Path -LiteralPath $qt -PathType Container)) {
        Add-Result 'Qt?' 'preflight' $false "missing Qt bin directory: $qt"
        continue
    }
    $major = if (Test-Path (Join-Path $qt 'Qt6Core.dll')) { 6 }
             elseif (Test-Path (Join-Path $qt 'Qt5Core.dll')) { 5 }
             else { 0 }
    if ($major -eq 0) {
        Add-Result $qt 'preflight' $false 'neither Qt6Core.dll nor Qt5Core.dll in that bin'
        continue
    }
    $qtRoot = Split-Path -Parent $qt

    $flavours = switch ($Library) {
        'Static' { @('static') }
        'Shared' { @('shared') }
        default { @('static', 'shared') }
    }

    foreach ($flavour in $flavours) {
        $isShared = ($flavour -eq 'shared')
        $sanitizerSuffix = if ($Asan) { '-asan' } else { '' }
        $treeName = "cmake-build-{0}-qt{1}{2}{3}" -f $configuration.ToLower(), $major, $(if ($isShared) { '-shared' } else { '' }), $sanitizerSuffix
        $combo = "Qt$major/$flavour" + $(if ($Asan) { ' asan' } else { '' }) + $(if ($Release) { ' release' } else { '' })
        $extraConfigure = @()
        if ($Asan) {
            $extraConfigure += '-DCMAKE_CXX_FLAGS=/fsanitize=address'
            $extraConfigure += '-DVIRTUALITEMVIEWS_BUILD_BENCHMARKS=OFF'
        }
        $consumerName = "cmake-build-consumer-qt{0}{1}{2}" -f $major, $(if ($isShared) { '-shared' } else { '' }), $(if ($Release) { '-release' } else { '' })

        Invoke-Kit -Combo $combo -Tree (Join-Path $repo $treeName) -QtRoot $qtRoot -QtBin $qt `
                   -Configuration $configuration -ExtraConfigure $extraConfigure `
                   -ConsumerName $consumerName `
                   -SharedOption $(if ($isShared) { 'ON' } else { 'OFF' })
    }
}

# -- MinGW / llvm-mingw kits (no vcvars: the kit's own compiler is put on PATH) ---

if ($MinGW) {
    if ($Asan) {
        Add-Result 'MinGW' 'preflight' $false '-Asan is MSVC only for now'
    } else {
        foreach ($kit in $MinGWKits) {
            $qt = $kit.QtBin
            $compilerBin = $kit.CompilerBin
            if (-not (Test-Path -LiteralPath $qt -PathType Container)) {
                Add-Result 'Qt?' 'preflight' $false "missing Qt bin directory: $qt"
                continue
            }
            if (-not (Test-Path -LiteralPath $compilerBin -PathType Container)) {
                Add-Result 'Qt?' 'preflight' $false "missing compiler bin directory: $compilerBin"
                continue
            }
            $major = if (Test-Path (Join-Path $qt 'Qt6Core.dll')) { 6 }
                     elseif (Test-Path (Join-Path $qt 'Qt5Core.dll')) { 5 }
                     else { 0 }
            if ($major -eq 0) {
                Add-Result $qt 'preflight' $false 'neither Qt6Core.dll nor Qt5Core.dll in that bin'
                continue
            }
            $qtRoot = Split-Path -Parent $qt
            $pathPrefix = @($compilerBin)
            if ($NinjaBin -and (Test-Path -LiteralPath $NinjaBin -PathType Container)) {
                $pathPrefix += $NinjaBin
            }
            $extraConfigure = @("-DCMAKE_CXX_COMPILER=$(Join-Path $compilerBin $kit.Cxx)",
                                "-DCMAKE_C_COMPILER=$(Join-Path $compilerBin $kit.Cc)")
            $sanitizerSuffix = ''
            if ($UBSan) {
                if ($kit.Name -ne $UBSanKit) {
                    continue
                }
                $extraConfigure += '-DCMAKE_CXX_FLAGS=-fsanitize=undefined -fno-sanitize-recover=undefined'
                $sanitizerSuffix = '-ubsan'
            }

            $flavours = switch ($Library) {
                'Static' { @('static') }
                'Shared' { @('shared') }
                default { @('static', 'shared') }
            }
            foreach ($flavour in $flavours) {
                $isShared = ($flavour -eq 'shared')
                # The configuration is part of the tree name, like it is for the MSVC
                # combos: a single-config generator cannot hold Debug and Release in one
                # tree, and switching CMAKE_BUILD_TYPE would silently replace the other.
                $treeName = "cmake-build-{0}-mingw-{1}{2}{3}" -f $configuration.ToLower(), $kit.Name, $(if ($isShared) { '-shared' } else { '' }), $sanitizerSuffix
                $combo = "Qt$major/mingw $($kit.Name)/$flavour" + $(if ($UBSan) { ' ubsan' } else { '' }) + $(if ($Release) { ' release' } else { '' })
                Invoke-Kit -Combo $combo -Tree (Join-Path $repo $treeName) -QtRoot $qtRoot -QtBin $qt `
                           -Configuration $configuration -ExtraConfigure $extraConfigure `
                           -PathPrefix $pathPrefix `
                           -ConsumerName "cmake-build-consumer-mingw-$($kit.Name)$(if ($isShared) { '-shared' } else { '' })" `
                           -SharedOption $(if ($isShared) { 'ON' } else { 'OFF' })
            }
        }
    }
}

# -- summary -----------------------------------------------------------------

$failed = @($results | Where-Object { -not $_.Ok })
Write-Host ""
Write-Host ("=" * 72)
Write-Host ("validation: {0} steps, {1} failed" -f $results.Count, $failed.Count)
foreach ($failure in $failed) {
    Write-Host ("  FAIL  {0,-16} {1} {2}" -f $failure.Step, $failure.Combo, $failure.Detail)
}
if ($failed.Count -eq 0) {
    Write-Host "all good"
}
exit $failed.Count
