FROM python:3.11-slim

WORKDIR /app

# Install dependencies
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Copy application files
COPY latency_measurement.py .
COPY config.toml .

# Run the application
CMD ["python", "latency_measurement.py"]
