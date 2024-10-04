import asyncio
import time
import aiohttp

max_token_len = 10

async def make_request_with_token(session, token):
    header = {'X-fake-auth': token}
    async with session.get('http://localhost:8000', headers=header) as response:
        return await response.status

async def find_token_len():
    async with aiohttp.ClientSession() as session:
        tasks = []
        for i in range(max_token_len):
            task = asyncio.ensure_future(make_request_with_token(session, 'THISISATEST'))
            tasks.append(task)
        await asyncio.gather(*tasks, return_exceptions=True)

if __name__ == "__main__":
    start_time = time.time()
    asyncio.get_event_loop().run_until_complete(find_token_len())
    duration = time.time() - start_time
