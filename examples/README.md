# amtail examples

## `zip()` (amtail extension)

`zip` walks several `split()` arrays in lockstep (like Python `zip`), stopping at the shortest array. Max arity is 8.

```mtail
$parts_a = split(",", $a)
$parts_b = split(",", $b)
zip($parts_a, $parts_b) as ($x, $y) {
  # one iteration per paired element
}
```

| Rule | Behavior |
|------|----------|
| Separator | Only the string you pass to `split` (e.g. `","`). Spaces after commas are trimmed. |
| Empty source | `split` on `""` → count `0` → `zip` body does not run |
| Missing array | `zip` short-circuits (0 iterations) |
| Arity | Number of arrays must match number of bind names |

Related: `range($arr) as $x { }` walks a single split array.

### Files

| File | What it shows |
|------|----------------|
| [`zip_basic.mtail`](zip_basic.mtail) | Parallel upstream fields → per-peer metrics |
| [`nginx_json.mtail`](nginx_json.mtail) | Nginx JSON access log: `","` retries, `" : "` fallback, histograms |
| [`nginx_error.mtail`](nginx_error.mtail) | Nginx `error_log`: severity + kind (+ server/zone/check-peer/lua) |
| [`nginx_error.log`](nginx_error.log) | Anonymized sample error_log for local testing |
| [`anonymize_nginx_error_log.py`](anonymize_nginx_error_log.py) | Redact IPs/hosts/cookies from a real dump → `nginx_error.log` |

### Local test (nginx error_log)

```bash
# from amtail/
./build/amtail --run examples/nginx_error.mtail examples/nginx_error.log

# or anonymize a fresh dump:
python3 examples/anonymize_nginx_error_log.py /path/to/error_log examples/nginx_error.log
```


Automated tests live under `tests/zip_*.mtail`.
