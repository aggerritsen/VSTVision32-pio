pio -t clean
pio run -e t-sim7070 -t upload; if ($LASTEXITCODE -eq 0) { pio device monitor }
