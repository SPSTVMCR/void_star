FROM python:3.12-slim

WORKDIR /app

# Install dependencies
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Copy all python files and pre-trained models
COPY . .

# Expose the port your Flask app uses
EXPOSE 5055

# Run the app using Waitress (which you included in requirements.txt)
CMD ["python", "lamp_preset_model.py", "--listen", "0.0.0.0", "--port", "5055", "--waitress"]