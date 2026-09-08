"""Regression for removing legacy fitting imports without breaking ingestion."""
import sqlite3
import numpy as np
from fastapi.testclient import TestClient


def test_measurement_lifecycle_after_fit_removal(tmp_path, monkeypatch):
    database = tmp_path / 'measurement.db'
    with sqlite3.connect(database) as db:
        db.execute('CREATE TABLE fitresults (id TEXT PRIMARY KEY, legacy TEXT)')
        db.execute("INSERT INTO fitresults VALUES ('old', 'preserve-me')")
    monkeypatch.setenv('LCR_DB_PATH', str(database))
    # Explicitly override the configured URL before loading database/application.
    from app import config
    monkeypatch.setattr(config, 'DB_URL', f'sqlite:///{tmp_path}/measurement.db')
    from app.main import app
    from app.services.simulator import MODELS, ordered_params, make_waveforms
    client = TestClient(app)
    assert client.get('/health').json() == {'ok': True}
    assert client.get('/api/models').status_code == 404
    scan = client.post('/api/scan/start', json={'device': 'test', 'freq_list': [1000]})
    assert scan.status_code == 200, scan.text
    scan_id = scan.json()['id']
    for model in MODELS:
        p = ordered_params(model, 50, 1e-3, 1e-6)
        v, i, dt, n = make_waveforms(model, p, 1000, 32, 8, .01, 0, 0, 0, np.random.default_rng(1))
        assert np.all(np.isfinite(v))
    v, i, dt, n = make_waveforms('series_RLC', [50, 1e-3, 1e-6], 1000, 32, 8, .01, 0, 0, 0, np.random.default_rng(1))
    result = client.post(f'/api/scan/{scan_id}/point', json={
        'device': 'test', 'frequency': 1000, 'dt': dt, 'n': n,
        'voltage': v.tolist(), 'current': i.tolist(),
    })
    assert result.status_code == 200, result.text
    assert abs(result.json()['z_real'] - 50) < 1e-8
    detail = client.get(f'/api/scan/{scan_id}')
    assert detail.status_code == 200
    assert len(detail.json()['measurements']) == 1
    client.close()
    with sqlite3.connect(database) as db:
        assert db.execute('SELECT legacy FROM fitresults WHERE id=?', ('old',)).fetchone() == ('preserve-me',)
