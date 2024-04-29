RAND=$(date +%s)
bin/sd -m ../models/sd_xl_base_1.0.safetensors --vae ../models/sdxl.vae.safetensors -H 1024 -W 1024 -s -1 -o share/$RAND -p "$1" -n "$2"
