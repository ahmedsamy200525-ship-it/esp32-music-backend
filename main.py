import os
import json
from typing import Optional

from fastapi import FastAPI, HTTPException, Request
from groq import Groq
from pydantic import BaseModel

app = FastAPI(title="ESP32 Alert Tone Generator Backend")

GROQ_API_KEY = os.environ.get("GROQ_API_KEY")
if not GROQ_API_KEY:
    raise RuntimeError("ERROR: Please set GROQ_API_KEY environment variable before running!")

client = Groq(api_key=GROQ_API_KEY)

# الموديل اللي كان شغال معاك من أول يوم
MODEL_NAME = "qwen/qwen3.8-27b"

SYSTEM_PROMPT = """
You are an AI sound designer for embedded systems. Generate a system alert tone, notification chime, or distinctive harmonic signal based on the user's request.

CRITICAL REQUIREMENTS:
1. Output STRICTLY valid JSON (no markdown, no explanations).
2. Keep it simple: 2-5 tones max.
3. Use standard frequencies (e.g., 440, 523, 659, 784, 880, 1046 Hz).
4. Duration: 100-500ms per tone.
5. Waveform: "sine" only.

OUTPUT JSON SCHEMA (MUST FOLLOW EXACTLY):
{
  "type": "alert",
  "description": "Short description of the sound",
  "tones": [
    {"frequency": 440, "duration_ms": 200, "waveform": "sine"},
    {"frequency": 523, "duration_ms": 300, "waveform": "sine"},
    {"frequency": 659, "duration_ms": 400, "waveform": "sine"}
  ]
}
"""


class PromptRequest(BaseModel):
    prompt: str


@app.post("/generate-melody")
def generate_melody_text(req: PromptRequest):
    return _generate_tones(req.prompt)


@app.post("/generate-melody-voice")
async def generate_melody_voice(request: Request):
    try:
        audio_content = await request.body()
        print(f"[Voice] Received {len(audio_content)} bytes")
        
        transcription = client.audio.transcriptions.create(
            model="whisper-large-v3-turbo",
            file=("audio.wav", audio_content, "audio/wav"),
            response_format="text",
            language="en"
        )
        user_text = transcription.strip()
        print(f"[Whisper Transcription]: {user_text}")
        return _generate_tones(user_text)
        
    except Exception as e:
        print(f"[Voice Processing Error]: {e}")
        raise HTTPException(status_code=500, detail=f"Voice processing failed: {str(e)}")


def _generate_tones(user_prompt: str):
    try:
        print(f"\n[Incoming Request]: {user_prompt}")
        completion = client.chat.completions.create(
            model=MODEL_NAME,
            messages=[
                {"role": "system", "content": SYSTEM_PROMPT},
                {"role": "user", "content": f"Generate an alert tone for: {user_prompt}"}
            ],
            temperature=0.3,
            max_tokens=500,
            response_format={"type": "json_object"}
        )
        data = json.loads(completion.choices[0].message.content)

        required_keys = ["type", "tones"]
        for key in required_keys:
            if key not in data:
                raise ValueError(f"Missing required field: {key}")

        print(f"[OK] {data.get('description')} | Tones: {len(data.get('tones', []))}")
        return data

    except json.JSONDecodeError as e:
        print(f"[JSON Parse Error]: {e}")
        raise HTTPException(status_code=500, detail="LLM returned invalid JSON")
    except Exception as e:
        print(f"[Backend Error]: {e}")
        raise HTTPException(status_code=500, detail=str(e))


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)