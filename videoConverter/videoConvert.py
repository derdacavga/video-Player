import os
import subprocess
import glob
import struct
import imageio_ffmpeg 

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
INPUT_DIR = os.path.join(SCRIPT_DIR, "input_videos")
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "output_sd")

FFMPEG_CMD = imageio_ffmpeg.get_ffmpeg_exe()

TARGET_FPS = 25

def convert_all():
    if not os.path.exists(INPUT_DIR):
        os.makedirs(INPUT_DIR)
        print(f"Created folder: {INPUT_DIR}")
        return

    if not os.path.exists(OUTPUT_DIR):
        os.makedirs(OUTPUT_DIR)

    files = glob.glob(os.path.join(INPUT_DIR, '*.*'))
    video_exts = ['.mp4', '.mkv', '.avi', '.mov', '.webm']
    files = [f for f in files if os.path.splitext(f)[1].lower() in video_exts]

    if not files:
        print("No videos found.")
        return

    print(f"Found {len(files)} videos...")

    for file_path in files:
        filename = os.path.splitext(os.path.basename(file_path))[0]
        clean_name = filename.replace(" ", "_")
        
        out_video = os.path.join(OUTPUT_DIR, f"{clean_name}.mjpeg")
        out_audio = os.path.join(OUTPUT_DIR, f"{clean_name}.wav")
        temp_video = os.path.join(OUTPUT_DIR, f"temp_{clean_name}.mjpeg")

        print(f"\nProcessing: {filename}")
        
        cmd_audio = [
            FFMPEG_CMD, '-y', '-i', file_path,
            '-vn', '-ac', '1', '-ar', '22050',
            '-acodec', 'pcm_s16le', out_audio
        ]

        cmd_video = [
            FFMPEG_CMD, '-y', '-i', file_path,
            '-vf', 'scale=320:-2', 
            '-c:v', 'mjpeg', '-q:v', '7', 
            '-r', str(TARGET_FPS), 
            '-an', temp_video
        ]

        try:
            subprocess.run(cmd_audio, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            subprocess.run(cmd_video, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            
            with open(temp_video, 'rb') as f_in:
                video_data = f_in.read()
            
            with open(out_video, 'wb') as f_out:
                f_out.write(struct.pack('B', TARGET_FPS)) 
                f_out.write(video_data)
            
            os.remove(temp_video)
            print(f"  [OK] Saved at 25 FPS.")
            
        except Exception as e:
            print(f"  [ERROR] {e}")

if __name__ == "__main__":
    convert_all()