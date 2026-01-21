pio run -t clean -e t-sim7080
pio run -e t-sim7080 -t upload; if ($LASTEXITCODE -eq 0) { pio device monitor }
