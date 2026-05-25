import { test, expect } from '@playwright/test';

test.describe('REST API', () => {
  test.beforeEach(async ({ request }) => {
    const res = await request.get('/api/sessions?pageSize=100&pageNumber=1');
    if (res.ok()) {
      const data = await res.json();
      for (const s of data.sessions || []) {
        await request.delete(`/api/sessions/${encodeURIComponent(s.name)}`);
      }
    }
  });

  test('GET /api/sessions returns empty list initially', async ({ request }) => {
    const res = await request.get('/api/sessions?pageSize=100&pageNumber=1');
    expect(res.status()).toBe(200);
    const data = await res.json();
    expect(data.sessions).toEqual([]);
  });

  test('POST /api/sessions creates session', async ({ request }) => {
    const res = await request.post('/api/sessions', {
      data: { name: 'api-test-1' },
    });
    expect(res.status()).toBe(201);

    const list = await request.get('/api/sessions?pageSize=100&pageNumber=1');
    const data = await list.json();
    expect(data.sessions).toHaveLength(1);
    expect(data.sessions[0].name).toBe('api-test-1');
  });

  test('POST /api/sessions duplicate returns 409', async ({ request }) => {
    await request.post('/api/sessions', { data: { name: 'dup-sess' } });
    const res = await request.post('/api/sessions', { data: { name: 'dup-sess' } });
    expect(res.status()).toBe(409);
  });

  test('DELETE /api/sessions/{name} removes session', async ({ request }) => {
    await request.post('/api/sessions', { data: { name: 'to-delete' } });
    const res = await request.delete('/api/sessions/to-delete');
    expect(res.status()).toBe(200);

    const list = await request.get('/api/sessions?pageSize=100&pageNumber=1');
    const data = await list.json();
    expect(data.sessions).toEqual([]);
  });

  test('DELETE /api/sessions/{nonexistent} returns 404', async ({ request }) => {
    const res = await request.delete('/api/sessions/does-not-exist');
    expect(res.status()).toBe(404);
  });

  test('pagination params work', async ({ request }) => {
    await request.post('/api/sessions', { data: { name: 'page-1' } });
    await request.post('/api/sessions', { data: { name: 'page-2' } });
    await request.post('/api/sessions', { data: { name: 'page-3' } });

    const res = await request.get('/api/sessions?pageSize=2&pageNumber=1');
    expect(res.status()).toBe(200);
    const data = await res.json();
    expect(data.sessions).toHaveLength(2);

    const res2 = await request.get('/api/sessions?pageSize=2&pageNumber=2');
    const data2 = await res2.json();
    expect(data2.sessions).toHaveLength(1);
  });
});
