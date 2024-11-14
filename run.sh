RAND=$(date +%s)
bin/sd -m ../models/sd_xl_base_1.0.safetensors --vae ../models/sdxl.vae.safetensors -H 1024 -W 1024 -s -1 -o share/$RAND -p "$1" -n "$2"
#bin/sd -m ../../models/sdxl_lightning_4step.safetensors --vae ../../models/sdxl.vae.safetensors -p "A beautiful girl" --steps 4 -H 1024 -W 1024 --cfg-scale 1 -s -1
