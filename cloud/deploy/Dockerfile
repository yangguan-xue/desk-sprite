FROM python:3.12-slim
ARG PIP_INDEX_URL=https://mirrors.cloud.tencent.com/pypi/simple
ENV PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1
WORKDIR /app
COPY cloud/requirements.txt /app/requirements.txt
RUN pip install --no-cache-dir -r requirements.txt
COPY cloud/app /app/cloud/app
COPY shared /app/shared
USER 10001:10001
CMD ["python", "-m", "uvicorn", "cloud.app.main:app", "--host", "0.0.0.0", "--port", "8000", "--workers", "1", "--root-path", "/sprite"]
