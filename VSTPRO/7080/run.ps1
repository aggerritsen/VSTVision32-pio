pio -t clean
pio run -e t-sim7080g-s3 -t upload; if ($LASTEXITCODE -eq 0) { pio device monitor }
