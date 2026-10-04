# CliJudge smoke regression - covers report fixes (EN output, ODR inline, import trust
# warning, config parse warning, usage strings, contest/article roundtrip, ide de-shell)
param(
    [string]$Bin = (Join-Path (Split-Path -Parent $PSScriptRoot) "clijudge.exe")
)
$ErrorActionPreference = "Continue"
$root = "$env:TEMP\cj_smoke_" + (Get-Random)
$data = "$root\data"
$work = "$root\work"
New-Item -ItemType Directory -Path $data, $work -Force | Out-Null
$env:CLIJUDGE_DATA_DIR = $data

$script:pass = 0
$script:fail = 0

function Run($argList) {
    $out = (& $bin @argList 2>&1 | Out-String)
    return @{ out = $out; code = $LASTEXITCODE }
}
function Check($name, $cond, $detail = "") {
    if ($cond) { $script:pass++; Write-Host "PASS: $name" }
    else { $script:fail++; Write-Host "FAIL: $name  $detail" }
}
function W($path, $content) { [IO.File]::WriteAllText($path, $content) }

# T01 help + displaylang line
$r = Run @("help")
Check "help exit0" ($r.code -eq 0) "code=$($r.code)"
Check "help has displaylang" ($r.out -match "displaylang") ""
Check "help has Usage" ($r.out -match "Usage") ""

# T02 problem create
$r = Run @("problem", "create", "A + B")
Check "problem create" ($r.out -match "Problem created with ID: 1") $r.out

# T02b 'problem create help' prints usage instead of creating a problem
$r = Run @("problem", "create", "help")
Check "create help usage" (($r.code -eq 1) -and ($r.out -match "Usage: clijudge.exe problem create")) "code=$($r.code) out=$($r.out)"

# T03 testdata (LF files, byte-exact compare)
W "$work\in.txt" "1 2`n"
W "$work\out.txt" "3`n"
$r = Run @("problem", "testdata", "1", "create", "$work\in.txt", "$work\out.txt", "1000", "256", "50")
Check "testdata create" ($r.out -match "Test case created with ID: 1") $r.out

# T04 submit AC
W "$work\ac.cpp" '#include <iostream>
int main(){long long a,b;std::cin>>a>>b;std::cout<<a+b<<std::endl;return 0;}'
$r = Run @("problem", "submit", "1", "$work\ac.cpp", "--as", "alice")
Check "submit AC" (($r.code -eq 0) -and ($r.out -match "Status: AC")) "code=$($r.code) out=$($r.out)"

# T04b submission cwd must not contain test data files (answer-leak regression)
$leakSrc = @'
#include <stdio.h>
int main(){
  const char* f[]={"test_input.txt","test_expected.txt","data.in","data.out"};
  for(int i=0;i<4;i++){FILE* p=fopen(f[i],"rb"); if(p){fclose(p); puts("LEAK"); return 0;}}
  long long a,b;
  if(scanf("%lld %lld",&a,&b)==2) printf("%lld\n",a+b); else printf("3\n");
  return 0;
}
'@
W "$work\leak.cpp" $leakSrc
$r = Run @("problem", "submit", "1", "$work\leak.cpp", "--as", "alice")
Check "submit cwd no test data" (($r.code -eq 0) -and ($r.out -match "Status: AC")) "code=$($r.code) out=$($r.out)"

# T05 export
$r = Run @("problem", "export", "1", "$work\p1.zip")
Check "problem export" ($r.out -match "Problem exported to:") $r.out

# T06 delete
$r = Run @("problem", "delete", "1")
Check "problem delete EN" ($r.out -match "Problem 1 deleted\.") $r.out

# T07 import zip without SPJ -> no trust warning (capture dynamic id)
$r = Run @("problem", "import", "$work\p1.zip")
Check "problem import" ($r.out -match "Problem imported with ID: \d+") $r.out
$impId = 0
if ($r.out -match "Problem imported with ID: (\d+)") { $impId = [int]$Matches[1] }
Check "import id captured" ($impId -gt 0) "impId=$impId"
Check "import no-spj no warning" (-not ($r.out -match "Warning: this package")) $r.out

# T08 import json WITH spj_code -> trust warning
$spjJson = @{
    problem    = @{
        title = "SPJ P"; time_limit = 1000; memory_limit = 256
        problem_type = "traditional"; compare_mode = "spj"
        spj_code     = "int main(){return 0;}"
    }
    test_cases = @()
} | ConvertTo-Json -Depth 6
W "$work\spj.json" $spjJson
$r = Run @("problem", "import", "$work\spj.json")
Check "spj import" ($r.out -match "Problem imported with ID") $r.out
Check "spj import warns trust" ($r.out -match "Warning: this package contains special judge") $r.out
Check "spj warning mentions trusted mode" ($r.out -match "reduced-\s*isolation") $r.out

# T09/T10 article EN outputs
$r = Run @("article", "create", "Hello")
Check "article create EN" ($r.out -match "Article created, ID: 1") $r.out
$r = Run @("article", "delete", "1")
Check "article delete EN" ($r.out -match "Article 1 deleted\.") $r.out
$r = Run @("article", "delete", "9")
Check "article notfound EN" (($r.code -eq 1) -and ($r.out -match "Article 9 not found\.")) $r.out

# T11 contest create with the dynamically imported problem id
$r = Run @("contest", "create", "C1", "2026-01-01 10:00:00", "2026-01-02 10:00:00", "$impId")
Check "contest create" ($r.out -match "Contest created with ID: 1") $r.out

# T12 contest export
$r = Run @("contest", "export", "1", "$work\c.cdf")
Check "contest export EN" ($r.out -match "Contest exported to:") $r.out

# T13 usage errors EN
$r = Run @("contest", "import")
Check "contest import usage EN" (($r.code -eq 1) -and ($r.out -match "Usage: clijudge\.exe contest import \[cdf_path\]")) $r.out
$r = Run @("contest", "export", "1")
Check "contest export usage EN" (($r.code -eq 1) -and ($r.out -match "Usage: clijudge\.exe contest export \[id\] \[cdf_path\]")) $r.out

# T14 contest import roundtrip (EN outputs, no warning for plain problem)
$r = Run @("contest", "import", "$work\c.cdf")
Check "contest import EN" ($r.out -match "Contest imported:") $r.out
Check "contest import problem line EN" ($r.out -match "  Imported problem:") $r.out
Check "contest import no-spj no warning" (-not ($r.out -match "Warning: this CDF")) $r.out

# T15 ide run EN outputs, stdin redirect (de-shell redesign)
W "$work\hello.cpp" '#include <iostream>
int main(){std::cout<<"hi"<<std::endl;return 0;}'
$r = Run @("ide", "run", "$work\hello.cpp")
Check "ide run no-input exit0" (($r.code -eq 0) -and ($r.out -match "hi")) "code=$($r.code) out=$($r.out)"
Check "ide run EN lines" (($r.out -match "Exit code: 0") -and ($r.out -match "Time: ") -and ($r.out -match "Memory: ")) $r.out
$r = Run @("ide", "run", "$work\ac.cpp", "$work\in.txt")
Check "ide run stdin redirect" (($r.code -eq 0) -and ($r.out -match "^3") -and ($r.out -match "Exit code: 0")) "code=$($r.code) out=$($r.out)"
W "$work\t.py" "import sys`na,b=map(int,sys.stdin.read().split())`nprint(a+b)"
$r = Run @("ide", "run", "$work\t.py", "$work\in.txt")
Check "ide run python" (($r.code -eq 0) -and ($r.out -match "^3")) "code=$($r.code) out=$($r.out)"
W "$work\bad.cpp" "int main(){return}"
$r = Run @("ide", "run", "$work\bad.cpp")
Check "ide run compile error" (($r.code -eq 1) -and ($r.out -match "error:")) "code=$($r.code) out=$($r.out)"
$r = Run @("ide", "run", "$work\missing.cpp")
Check "ide run missing file" (($r.code -eq 1) -and ($r.out -match "Error: code file not found")) $r.out

# T16 report generated EN
$r = Run @("contest", "report", "1", "$work\rep.html")
Check "contest report EN" ($r.out -match "Contest report generated:") $r.out

# T16b custom language (compiled) via ide + submit - new de-shell chain
$cfgPath = "$data\config.json"
W $cfgPath '{"current_lang":"en","judge":{"custom_languages":{"jl":{"extensions":[".jlang"],"compile":"g++ -O2 -x c++ -o {exe} {src}","run":"{exe}"}}}}'
W "$work\a.jlang" '#include <iostream>
int main(){long long a,b;std::cin>>a>>b;std::cout<<a+b<<std::endl;return 0;}'
$r = Run @("ide", "run", "$work\a.jlang", "$work\in.txt")
Check "ide custom-lang compiled" (($r.code -eq 0) -and ($r.out -match "^3")) "code=$($r.code) out=$($r.out)"
$r = Run @("problem", "create", "JL")
$jlId = 0
if ($r.out -match "Problem created with ID: (\d+)") { $jlId = [int]$Matches[1] }
Check "jl problem create" ($jlId -gt 0) $r.out
$r = Run @("problem", "testdata", "$jlId", "create", "$work\in.txt", "$work\out.txt", "1000", "256", "50")
Check "jl testdata" ($r.out -match "Test case created") $r.out
$r = Run @("problem", "submit", "$jlId", "$work\a.jlang", "--as", "bob")
Check "jl submit AC" (($r.code -eq 0) -and ($r.out -match "Status: AC")) "code=$($r.code) out=$($r.out)"

# T17 corrupt config.json -> parse warning (LAST: restore after)
$cfgPath = "$data\config.json"
$cfgOrig = [IO.File]::ReadAllText($cfgPath)
W $cfgPath "{ this is not json"
$r = Run @("problem", "count")
Check "corrupt config warns" ($r.out -match "failed\s+to\s+parse[\s\S]*config\.json") $r.out
[IO.File]::WriteAllText($cfgPath, $cfgOrig)
$r = Run @("problem", "count")
Check "restored config clean" (($r.code -eq 0) -and (-not ($r.out -match "Warning:"))) "code=$($r.code) out=$($r.out)"

# T18 non-ASCII argv/path: Chinese title roundtrip + Chinese zip name (UTF-8 console/argv)
$r = Run @("problem", "create", "中文标题测试")
$zhId = 0
if ($r.out -match "Problem created with ID: (\d+)") { $zhId = [int]$Matches[1] }
Check "zh create" ($zhId -gt 0) $r.out
$zhIdx = [IO.File]::ReadAllText("$data\problems\problems.json", [Text.UTF8Encoding]::new($false))
Check "zh title stored UTF-8" ($zhIdx -match "中文标题测试") ""
$r = Run @("problem", "export", "$zhId", "$work\中文导出.zip")
Check "zh export path" (($r.code -eq 0) -and (Test-Path "$work\中文导出.zip")) $r.out
$r = Run @("problem", "delete", "$zhId")
Check "zh delete" ($r.out -match "Problem $zhId deleted\.") $r.out

# T19 built-in language 'en' must not be deletable
$r = Run @("displaylang", "delete", "en")
Check "delete builtin en refused" (($r.code -eq 1) -and ($r.out -match "Built-in language 'en' cannot be deleted")) "code=$($r.code) out=$($r.out)"

# T20 non-object config.json -> warn + graceful unconfigured exit (D1), BOM config -> parses cleanly (D2)
$cfgPath = "$data\config.json"
$cfgOrig = [IO.File]::ReadAllText($cfgPath)
W $cfgPath '[1,2,3]'
$r = Run @("problem", "count")
Check "non-object config warns" ($r.out -match "is not a JSON\s+object") $r.out
Check "non-object config graceful" (($r.code -eq 1) -and ($r.out -match "No display language\s+configured")) "code=$($r.code) out=$($r.out)"
[IO.File]::WriteAllBytes($cfgPath, ([byte[]](0xEF, 0xBB, 0xBF)) + [Text.Encoding]::UTF8.GetBytes('{"current_lang":"en"}'))
$r = Run @("problem", "count")
Check "BOM config parses" (($r.code -eq 0) -and (-not ($r.out -match "failed\s+to\s+parse"))) "code=$($r.code) out=$($r.out)"
[IO.File]::WriteAllText($cfgPath, $cfgOrig)

# T21 text_no_space: tokens equal but line grouping differs -> PE (A3)
$r = Run @("problem", "create", "PE test", "-compare", "text_no_space")
$peId = 0
if ($r.out -match "Problem created with ID: (\d+)") { $peId = [int]$Matches[1] }
Check "pe problem create" ($peId -gt 0) $r.out
W "$work\pe_in.txt" "0`n"
W "$work\pe_exp.txt" "1`n2 3`n"
$r = Run @("problem", "testdata", "$peId", "create", "$work\pe_in.txt", "$work\pe_exp.txt", "1000", "256", "50")
Check "pe testdata" ($r.out -match "Test case created") $r.out
W "$work\pe.cpp" '#include <iostream>
int main(){std::cout<<"1 2\n3\n";return 0;}'
$r = Run @("problem", "submit", "$peId", "$work\pe.cpp", "--as", "carol")
Check "submit PE" (($r.code -eq 1) -and ($r.out -match "Status: PE")) "code=$($r.code) out=$($r.out)"

# T22 CDF custom-field roundtrip (C1/C2/C4): float mode + tolerances + per-tc time limit
$r = Run @("problem", "create", "FloatRT", "-compare", "float_rel", "-float-abs", "0", "-float-rel", "1e-6")
$ftId = 0
if ($r.out -match "Problem created with ID: (\d+)") { $ftId = [int]$Matches[1] }
Check "float problem create" ($ftId -gt 0) $r.out
W "$work\fin.txt" "0`n"
W "$work\fout.txt" "0.0`n"
$r = Run @("problem", "testdata", "$ftId", "create", "$work\fin.txt", "$work\fout.txt", "5000", "256", "50")
Check "float testdata" ($r.out -match "Test case created") $r.out
$r = Run @("problem", "view", "$ftId")
Check "float view fields" (($r.out -match "Compare Mode: float_rel") -and ($r.out -match "Float Rel Tolerance: 1e-06")) $r.out
$r = Run @("contest", "create", "CFT", "2026-01-01 10:00:00", "2026-01-02 10:00:00", "$ftId")
$ftCid = 0
if ($r.out -match "Contest created with ID: (\d+)") { $ftCid = [int]$Matches[1] }
Check "float contest create" ($ftCid -gt 0) $r.out
$r = Run @("contest", "export", "$ftCid", "$work\ft.cdf")
Check "float contest export" ($r.out -match "Contest exported to:") $r.out
$cdfText = [IO.File]::ReadAllText("$work\ft.cdf", [Text.UTF8Encoding]::new($false))
Check "cdf has compareModeCliJudge" ($cdfText -match '"compareModeCliJudge":\s*"float_rel"') ""
Check "cdf has floatRelTol" ($cdfText -match '"floatRelTol":\s*(1e-06|0\.000001)') ""
Check "cdf has floatAbsTol 0" ($cdfText -match '"floatAbsTol":\s*0') ""
Check "cdf tc timeLimit 5000" ($cdfText -match '"timeLimit":\s*5000') ""
$r = Run @("contest", "import", "$work\ft.cdf")
Check "float contest import" ($r.out -match "Contest imported:") $r.out
$nId = 0
if ($r.out -match "Imported problem:.*\(ID: (\d+)\)") { $nId = [int]$Matches[1] }
Check "float reimport id" ($nId -gt 0) $r.out
$r = Run @("problem", "view", "$nId")
Check "reimport compare mode" ($r.out -match "Compare Mode: float_rel") $r.out
Check "reimport rel tolerance" ($r.out -match "Float Rel Tolerance: 1e-06") $r.out
$r = Run @("problem", "testdata", "$nId")
Check "reimport tc time 5000" ($r.out -match '"time_limit": 5000') $r.out

# T23 lang source: query default / invalid rejected / set+persist roundtrip (gitee mirror)
$r = Run @("displaylang", "source")
Check "source query default" (($r.code -eq 0) -and ($r.out -match "Language pack source: github")) "code=$($r.code) out=$($r.out)"
$r = Run @("displaylang", "source", "bogus")
Check "source invalid rejected" (($r.code -eq 1) -and ($r.out -match "Invalid source: bogus")) "code=$($r.code) out=$($r.out)"
$r = Run @("displaylang", "source", "gitee")
Check "source set gitee" (($r.code -eq 0) -and ($r.out -match "Language pack source set to: gitee")) $r.out
$srcCfg = [IO.File]::ReadAllText($cfgPath)
Check "source persisted in config" ($srcCfg -match '"lang_source":\s*"gitee"') $srcCfg
$r = Run @("displaylang", "source", "github")
$srcCfg = [IO.File]::ReadAllText($cfgPath)
Check "source restore github" (($r.code -eq 0) -and ($srcCfg -match '"lang_source":\s*"github"')) $r.out
$r = Run @("displaylang", "source", "custom")
Check "source custom no-url usage" (($r.code -eq 1) -and ($r.out -match "Usage: clijudge displaylang source custom")) "code=$($r.code) out=$($r.out)"
$r = Run @("displaylang", "source", "custom", "not-a-url")
Check "source custom invalid url" (($r.code -eq 1) -and ($r.out -match "Invalid source URL: not-a-url")) "code=$($r.code) out=$($r.out)"

# T24 SPJ: input/expected written to dataDir after contestant run (SPJ checks actual == "3\n")
$spjChecker = '#include <stdio.h>
int main(int argc,char**argv){if(argc<3)return 1;FILE*f=fopen(argv[2],"rb");if(!f)return 1;char b[64]={0};size_t n=fread(b,1,63,f);fclose(f);return (n>=2&&b[0]==''3''&&(b[1]==''\n''||b[1]==''\r''))?0:1;}'
$spjRunJson = @{
    problem    = @{
        title = "SPJ post-run data"; time_limit = 1000; memory_limit = 256
        problem_type = "traditional"; compare_mode = "spj"
        spj_code     = $spjChecker
    }
    test_cases = @(@{ input_data = "1 2`n"; output_data = "3`n"; score = 100 })
} | ConvertTo-Json -Depth 6
W "$work\spjrun.json" $spjRunJson
$r = Run @("problem", "import", "$work\spjrun.json")
$spjId = 0
if ($r.out -match "Problem imported with ID: (\d+)") { $spjId = [int]$Matches[1] }
Check "spj-run import" ($spjId -gt 0) $r.out
$r = Run @("problem", "submit", "$spjId", "$work\ac.cpp", "--as", "dave")
Check "spj submit AC" (($r.code -eq 0) -and ($r.out -match "Status: AC")) "code=$($r.code) out=$($r.out)"
$r = Run @("problem", "submit", "$spjId", "$work\leak.cpp", "--as", "dave")
Check "spj cwd no test data" (($r.code -eq 0) -and ($r.out -match "Status: AC")) "code=$($r.code) out=$($r.out)"

Write-Host ""
Write-Host "PASS: $script:pass  FAIL: $script:fail"
Remove-Item -Path $root -Recurse -Force -ErrorAction SilentlyContinue
if ($script:fail -gt 0) { exit 1 } else { exit 0 }
