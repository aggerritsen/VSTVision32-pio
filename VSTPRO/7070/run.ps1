pio run -t clean -e t-sim7070
pio run -e t-sim7070 -t upload; if ($LASTEXITCODE -eq 0) { pio device monitor }
