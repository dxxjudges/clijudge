#!/bin/bash
# CliJudge Linux smoke - report fixes + ide de-shell + custom language
set -u
BIN="${CIJUDGE_BIN:-$(cd "$(dirname "$0")/.." && pwd)/clijudge}"
export CLIJUDGE_DATA_DIR=/tmp/cj_lx_smoke/data
W=/tmp/cj_lx_smoke/work
rm -rf /tmp/cj_lx_smoke
mkdir -p "$CLIJUDGE_DATA_DIR" "$W"
pass=0; fail=0
t() { # t <name> <0=pass> [detail]
  if [ "$2" -eq 0 ]; then pass=$((pass+1)); echo "PASS: $1"; else fail=$((fail+1)); echo "FAIL: $1  ${3:-}"; fi
}
run() { OUT=$("$BIN" "$@" 2>&1); CODE=$?; }

# environment probes (diagnose CI vs local sandbox behavior)
export CLIJUDGE_SANDBOX_DEBUG=/tmp/cj_lx_smoke/sbx.dbg
echo "ENV: uname=$(uname -r)"
echo "ENV: apparmor_restrict_unprivileged_userns=$(cat /proc/sys/kernel/apparmor_restrict_unprivileged_userns 2>/dev/null || echo n/a)"
echo "ENV: max_user_namespaces=$(cat /proc/sys/user/max_user_namespaces 2>/dev/null || echo n/a)"
echo "ENV: aa_profile=$(cat /proc/self/attr/current 2>/dev/null || echo n/a)"
if unshare -U true; then echo "ENV: unshare -U ok"; else echo "ENV: unshare -U DENIED rc=$?"; fi
if unshare -U -r true; then echo "ENV: unshare -U -r (self-map) ok"; else echo "ENV: unshare -U -r (self-map) DENIED rc=$?"; fi

run help
[ $CODE -eq 0 ] && echo "$OUT" | grep -q Usage && echo "$OUT" | grep -q displaylang; t "help exit0+usage+displaylang" $? "$OUT"

run problem create "A + B"
echo "$OUT" | grep -q "Problem created with ID: 1"; t "problem create" $? "$OUT"

# 'problem create help' prints usage instead of creating a problem
run problem create help
[ $CODE -eq 1 ] && echo "$OUT" | grep -q "Usage: clijudge.exe problem create"; t "create help usage" $? "code=$CODE $OUT"

printf '1 2\n' > "$W/in.txt"; printf '3\n' > "$W/out.txt"
run problem testdata 1 create "$W/in.txt" "$W/out.txt" 1000 256 50
echo "$OUT" | grep -q "Test case created with ID: 1"; t "testdata create" $? "$OUT"

cat > "$W/ac.cpp" <<'EOF'
#include <iostream>
int main(){long long a,b;std::cin>>a>>b;std::cout<<a+b<<std::endl;return 0;}
EOF
run problem submit 1 "$W/ac.cpp" --as alice
[ $CODE -eq 0 ] && echo "$OUT" | grep -q "Status: AC"; t "submit AC" $? "code=$CODE $OUT"

# submission cwd must not contain test data files (answer-leak regression)
cat > "$W/leak.cpp" <<'EOF'
#include <stdio.h>
int main(){
  const char* f[]={"test_input.txt","test_expected.txt","data.in","data.out"};
  for(int i=0;i<4;i++){FILE* p=fopen(f[i],"rb"); if(p){fclose(p); puts("LEAK"); return 0;}}
  long long a,b;
  if(scanf("%lld %lld",&a,&b)==2) printf("%lld\n",a+b); else printf("3\n");
  return 0;
}
EOF
run problem submit 1 "$W/leak.cpp" --as alice
[ $CODE -eq 0 ] && echo "$OUT" | grep -q "Status: AC"; t "submit cwd no test data" $? "code=$CODE $OUT"

run problem export 1 "$W/p1.zip"
echo "$OUT" | grep -q "Problem exported to:"; t "problem export" $? "$OUT"

run problem delete 1
echo "$OUT" | grep -q "Problem 1 deleted\."; t "problem delete EN" $? "$OUT"

run problem import "$W/p1.zip"
echo "$OUT" | grep -q "Problem imported with ID:"; t "problem import" $? "$OUT"
IMP=$(echo "$OUT" | sed -n 's/.*Problem imported with ID: \([0-9]*\).*/\1/p' | head -1)
[ -n "$IMP" ]; t "import id captured ($IMP)" $? "$OUT"
echo "$OUT" | grep -q "Warning: this package"; r=$?; [ $r -ne 0 ]; t "import no-spj no warning" $? "$OUT"

cat > "$W/spj.json" <<EOF
{"problem":{"title":"SPJ P","time_limit":1000,"memory_limit":256,"problem_type":"traditional","compare_mode":"spj","spj_code":"int main(){return 0;}"},"test_cases":[]}
EOF
run problem import "$W/spj.json"
echo "$OUT" | grep -q "Problem imported with ID"; t "spj import" $? "$OUT"
echo "$OUT" | grep -q "Warning: this package contains special judge"; t "spj import warns trust" $? "$OUT"
echo "$OUT" | grep -q "reduced-isolation"; t "spj warning trusted mode" $? "$OUT"

run article create Hello
echo "$OUT" | grep -q "Article created, ID: 1"; t "article create EN" $? "$OUT"
run article delete 1
echo "$OUT" | grep -q "Article 1 deleted\."; t "article delete EN" $? "$OUT"
run article delete 9
[ $CODE -eq 1 ] && echo "$OUT" | grep -q "Article 9 not found\."; t "article notfound EN" $? "$OUT"

run contest create C1 "2026-01-01 10:00:00" "2026-01-02 10:00:00" "$IMP"
echo "$OUT" | grep -q "Contest created with ID: 1"; t "contest create" $? "$OUT"

run contest export 1 "$W/c.cdf"
echo "$OUT" | grep -q "Contest exported to:"; t "contest export EN" $? "$OUT"

run contest import
[ $CODE -eq 1 ] && echo "$OUT" | grep -q "Usage: clijudge.exe contest import \[cdf_path\]"; t "contest import usage EN" $? "$OUT"
run contest export 1
[ $CODE -eq 1 ] && echo "$OUT" | grep -q "Usage: clijudge.exe contest export \[id\] \[cdf_path\]"; t "contest export usage EN" $? "$OUT"

run contest import "$W/c.cdf"
echo "$OUT" | grep -q "Contest imported:"; t "contest import EN" $? "$OUT"
echo "$OUT" | grep -q "  Imported problem:"; t "contest import problem line EN" $? "$OUT"
echo "$OUT" | grep -q "Warning: this CDF"; r=$?; [ $r -ne 0 ]; t "contest import no-spj no warning" $? "$OUT"

cat > "$W/hello.cpp" <<'EOF'
#include <iostream>
int main(){std::cout<<"hi"<<std::endl;return 0;}
EOF
run ide run "$W/hello.cpp"
[ $CODE -eq 0 ] && echo "$OUT" | grep -q hi && echo "$OUT" | grep -q "Exit code: 0" && echo "$OUT" | grep -q "Time: "; t "ide run c++" $? "code=$CODE $OUT"

run ide run "$W/ac.cpp" "$W/in.txt"
[ $CODE -eq 0 ] && echo "$OUT" | grep -q "^3" && echo "$OUT" | grep -q "Exit code: 0"; t "ide run stdin redirect" $? "code=$CODE $OUT"

cat > "$W/t.py" <<'EOF'
import sys
a,b=map(int,sys.stdin.read().split())
print(a+b)
EOF
run ide run "$W/t.py" "$W/in.txt"
[ $CODE -eq 0 ] && echo "$OUT" | grep -q "^3"; t "ide run python" $? "code=$CODE $OUT"

echo 'int main(){return}' > "$W/bad.cpp"
run ide run "$W/bad.cpp"
[ $CODE -eq 1 ] && echo "$OUT" | grep -q "error:"; t "ide run compile error" $? "code=$CODE $OUT"

run ide run "$W/missing.cpp"
[ $CODE -eq 1 ] && echo "$OUT" | grep -q "Error: code file not found"; t "ide run missing file" $? "$OUT"

run contest report 1 "$W/rep.html"
echo "$OUT" | grep -q "Contest report generated:"; t "contest report EN" $? "$OUT"

# corrupt config -> parse warning
CFG="$CLIJUDGE_DATA_DIR/config.json"
CFG_ORIG=$(cat "$CFG")
echo '{ this is not json' > "$CFG"
run problem count
echo "$OUT" | grep -q "Warning: failed to parse .*config\.json"; t "corrupt config warns" $? "$OUT"
printf '%s' "$CFG_ORIG" > "$CFG"
run problem count
[ $CODE -eq 0 ] && ! echo "$OUT" | grep -q "Warning:"; t "restored config clean" $? "code=$CODE $OUT"

# custom language: compiled (g++ {exe}) via ide + submit
python3 - "$CFG" <<'PYEOF'
import json,sys
p=sys.argv[1]
cfg=json.load(open(p))
cfg.setdefault('judge',{})['custom_languages']={'jl':{'extensions':['.jlang'],'compile':'g++ -O2 -x c++ -o {exe} {src}','run':'{exe}'}}
json.dump(cfg,open(p,'w'))
PYEOF
ck_rc=$?; t "config custom lang written" $ck_rc
cat > "$W/a.jlang" <<'EOF'
#include <iostream>
int main(){long long a,b;std::cin>>a>>b;std::cout<<a+b<<std::endl;return 0;}
EOF
run ide run "$W/a.jlang" "$W/in.txt"
[ $CODE -eq 0 ] && echo "$OUT" | grep -q "^3"; t "ide custom-lang compiled" $? "code=$CODE $OUT"

run problem create "JL"
echo "$OUT" | grep -q "Problem created with ID"; t "jl problem create" $? "$OUT"
JLID=$(echo "$OUT" | sed -n 's/.*ID: \([0-9]*\).*/\1/p' | head -1)
run problem testdata "$JLID" create "$W/in.txt" "$W/out.txt" 1000 256 50
echo "$OUT" | grep -q "Test case created"; t "jl testdata" $? "$OUT"
run problem submit "$JLID" "$W/a.jlang" --as bob
[ $CODE -eq 0 ] && echo "$OUT" | grep -q "Status: AC"; t "jl submit AC" $? "code=$CODE $OUT"

# custom language: bare command (PATH resolution), script-style (no compile)
mkdir -p /tmp/cj_lx_smoke/bin
cat > /tmp/cj_lx_smoke/bin/judgelang <<'EOF'
#!/bin/bash
# usage: judgelang <src>  - reads stdin, prints a+b
read a b
echo $((a+b))
EOF
chmod +x /tmp/cj_lx_smoke/bin/judgelang
python3 - "$CFG" <<'PYEOF'
import json,sys
p=sys.argv[1]
cfg=json.load(open(p))
cfg.setdefault('judge',{})['custom_languages']={'bare':{'extensions':['.bglang'],'compile':'','run':'judgelang {src}'}}
json.dump(cfg,open(p,'w'))
PYEOF
export PATH=/tmp/cj_lx_smoke/bin:$PATH
cat > "$W/b.bglang" <<'EOF'
dummy source resolved by judgelang shim
EOF
run ide run "$W/b.bglang" "$W/in.txt"
[ $CODE -eq 0 ] && echo "$OUT" | grep -q "^3"; t "ide custom-lang bare PATH" $? "code=$CODE $OUT"

run problem create "BG"
BGID=$(echo "$OUT" | sed -n 's/.*ID: \([0-9]*\).*/\1/p' | head -1)
run problem testdata "$BGID" create "$W/in.txt" "$W/out.txt" 1000 256 50
run problem submit "$BGID" "$W/b.bglang" --as bob
[ $CODE -eq 0 ] && echo "$OUT" | grep -q "Status: AC"; t "bare custom-lang submit AC" $? "code=$CODE $OUT"

# non-ASCII argv/path: Chinese title roundtrip + Chinese zip export
run problem create "中文标题测试"
ZID=$(echo "$OUT" | sed -n 's/.*Problem created with ID: \([0-9]*\).*/\1/p' | head -1)
[ -n "$ZID" ]; t "zh create" $? "$OUT"
grep -q "中文标题测试" "$CLIJUDGE_DATA_DIR/problems/problems.json"; t "zh title UTF-8" $?
run problem export "$ZID" "$W/中文导出.zip"
[ $CODE -eq 0 ] && [ -f "$W/中文导出.zip" ]; t "zh export path" $? "$OUT"
run problem delete "$ZID"
echo "$OUT" | grep -q "Problem $ZID deleted\."; t "zh delete" $? "$OUT"

# built-in language 'en' must not be deletable
run displaylang delete en
[ $CODE -eq 1 ] && echo "$OUT" | grep -q "Built-in language 'en' cannot be deleted"; t "delete builtin en refused" $? "code=$CODE $OUT"

# non-object config -> warn + graceful unconfigured exit (D1); BOM config -> parses cleanly (D2)
CFG="$CLIJUDGE_DATA_DIR/config.json"
CFG_ORIG2=$(cat "$CFG")
echo '[1,2,3]' > "$CFG"
run problem count
echo "$OUT" | grep -q "is not a JSON object"; t "non-object config warns" $? "$OUT"
[ $CODE -eq 1 ] && echo "$OUT" | grep -q "No display language configured"; t "non-object config graceful" $? "code=$CODE $OUT"
printf '\xEF\xBB\xBF{"current_lang":"en"}' > "$CFG"
run problem count
[ $CODE -eq 0 ] && ! echo "$OUT" | grep -q "failed to parse"; t "BOM config parses" $? "code=$CODE $OUT"
printf '%s' "$CFG_ORIG2" > "$CFG"

# text_no_space: tokens equal but line grouping differs -> PE (A3)
run problem create "PE test" -compare text_no_space
PEID=$(echo "$OUT" | sed -n 's/.*Problem created with ID: \([0-9]*\).*/\1/p' | head -1)
[ -n "$PEID" ]; t "pe problem create" $? "$OUT"
printf '0\n' > "$W/pe_in.txt"; printf '1\n2 3\n' > "$W/pe_exp.txt"
run problem testdata "$PEID" create "$W/pe_in.txt" "$W/pe_exp.txt" 1000 256 50
echo "$OUT" | grep -q "Test case created"; t "pe testdata" $? "$OUT"
cat > "$W/pe.cpp" <<'EOF'
#include <iostream>
int main(){std::cout<<"1 2\n3\n";return 0;}
EOF
run problem submit "$PEID" "$W/pe.cpp" --as carol
[ $CODE -eq 1 ] && echo "$OUT" | grep -q "Status: PE"; t "submit PE" $? "code=$CODE $OUT"

# CDF custom-field roundtrip (C1/C2/C4): float mode + tolerances + per-tc time limit
run problem create "FloatRT" -compare float_rel -float-abs 0 -float-rel 1e-6
FTID=$(echo "$OUT" | sed -n 's/.*Problem created with ID: \([0-9]*\).*/\1/p' | head -1)
[ -n "$FTID" ]; t "float problem create" $? "$OUT"
printf '0\n' > "$W/fin.txt"; printf '0.0\n' > "$W/fout.txt"
run problem testdata "$FTID" create "$W/fin.txt" "$W/fout.txt" 5000 256 50
echo "$OUT" | grep -q "Test case created"; t "float testdata" $? "$OUT"
run problem view "$FTID"
echo "$OUT" | grep -q "Compare Mode: float_rel" && echo "$OUT" | grep -q "Float Rel Tolerance: 1e-06"; t "float view fields" $? "$OUT"
run contest create CFT "2026-01-01 10:00:00" "2026-01-02 10:00:00" "$FTID"
FTCID=$(echo "$OUT" | sed -n 's/.*Contest created with ID: \([0-9]*\).*/\1/p' | head -1)
[ -n "$FTCID" ]; t "float contest create" $? "$OUT"
run contest export "$FTCID" "$W/ft.cdf"
echo "$OUT" | grep -q "Contest exported to:"; t "float contest export" $? "$OUT"
grep -Eq '"compareModeCliJudge": *"float_rel"' "$W/ft.cdf"; r1=$?
grep -Eq '"floatRelTol": *(1e-06|0\.000001)' "$W/ft.cdf"; r2=$?
grep -Eq '"floatAbsTol": *0' "$W/ft.cdf"; r3=$?
grep -Eq '"timeLimit": *5000' "$W/ft.cdf"; r4=$?
[ $r1 -eq 0 ] && [ $r2 -eq 0 ] && [ $r3 -eq 0 ] && [ $r4 -eq 0 ]; t "cdf custom fields (mode/rel/abs/tc-time)" $? "r=$r1/$r2/$r3/$r4"
run contest import "$W/ft.cdf"
echo "$OUT" | grep -q "Contest imported:"; t "float contest import" $? "$OUT"
NID=$(echo "$OUT" | sed -n 's/.*Imported problem:.*ID: \([0-9]*\).*/\1/p' | head -1)
[ -n "$NID" ]; t "float reimport id ($NID)" $? "$OUT"
run problem view "$NID"
echo "$OUT" | grep -q "Compare Mode: float_rel"; t "reimport compare mode" $? "$OUT"
echo "$OUT" | grep -q "Float Rel Tolerance: 1e-06"; t "reimport rel tolerance" $? "$OUT"
run problem testdata "$NID"
echo "$OUT" | grep -q '"time_limit": 5000'; t "reimport tc time 5000" $? "$OUT"

# lang source: query default / invalid rejected / set+persist roundtrip (gitee mirror)
run displaylang source
[ $CODE -eq 0 ] && echo "$OUT" | grep -q "Language pack source: github"; t "source query default" $? "code=$CODE $OUT"
run displaylang source bogus
[ $CODE -eq 1 ] && echo "$OUT" | grep -q "Invalid source: bogus"; t "source invalid rejected" $? "code=$CODE $OUT"
run displaylang source gitee
[ $CODE -eq 0 ] && echo "$OUT" | grep -q "Language pack source set to: gitee"; t "source set gitee" $? "code=$CODE $OUT"
grep -Eq '"lang_source": *"gitee"' "$CFG"; t "source persisted in config" $?
run displaylang source github
[ $CODE -eq 0 ] && grep -Eq '"lang_source": *"github"' "$CFG"; t "source restore github" $? "code=$CODE $OUT"
run displaylang source custom
[ $CODE -eq 1 ] && echo "$OUT" | grep -q "Usage: clijudge displaylang source custom"; t "source custom no-url usage" $? "code=$CODE $OUT"
run displaylang source custom not-a-url
[ $CODE -eq 1 ] && echo "$OUT" | grep -q "Invalid source URL: not-a-url"; t "source custom invalid url" $? "code=$CODE $OUT"

# SPJ: input/expected written to dataDir after contestant run (SPJ checks actual == "3\n")
cat > "$W/spjrun.json" <<'EOF'
{"problem":{"title":"SPJ post-run data","time_limit":1000,"memory_limit":256,"problem_type":"traditional","compare_mode":"spj","spj_code":"#include <stdio.h>\nint main(int argc,char**argv){if(argc<3)return 1;FILE*f=fopen(argv[2],\"rb\");if(!f)return 1;char b[64]={0};size_t n=fread(b,1,63,f);fclose(f);return (n>=2&&b[0]=='3'&&(b[1]=='\\n'||b[1]=='\\r'))?0:1;}"},"test_cases":[{"input_data":"1 2\n","output_data":"3\n","score":100}]}
EOF
run problem import "$W/spjrun.json"
SPJID=$(echo "$OUT" | sed -n 's/.*Problem imported with ID: \([0-9]*\).*/\1/p' | head -1)
[ -n "$SPJID" ]; t "spj-run import ($SPJID)" $? "$OUT"
run problem submit "$SPJID" "$W/ac.cpp" --as dave
[ $CODE -eq 0 ] && echo "$OUT" | grep -q "Status: AC"; t "spj submit AC" $? "code=$CODE $OUT"
run problem submit "$SPJID" "$W/leak.cpp" --as dave
[ $CODE -eq 0 ] && echo "$OUT" | grep -q "Status: AC"; t "spj cwd no test data" $? "code=$CODE $OUT"

echo ""
echo "PASS: $pass  FAIL: $fail"
if [ -s "$CLIJUDGE_SANDBOX_DEBUG" ]; then
  echo "--- sandbox debug log ---"
  sort -u "$CLIJUDGE_SANDBOX_DEBUG" | head -40
fi
rm -rf /tmp/cj_lx_smoke
[ $fail -eq 0 ] && exit 0 || exit 1
