"""Live waveform WebSocket."""
from __future__ import annotations
from fastapi import APIRouter, WebSocket, WebSocketDisconnect
from ..services.hub import hub

router = APIRouter(tags=["live"])


@router.websocket("/ws/live")
async def ws_live(ws: WebSocket):
    await hub.connect(ws)
    try:
        while True:
            await ws.receive_text()  # keep the socket open; ignore inbound
    except WebSocketDisconnect:
        hub.disconnect(ws)
