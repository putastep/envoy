#!/usr/bin/env python3
"""
Streaming Backend Server for Buffer Watermarking Tests
Supports various response patterns to test buffer behavior
"""

from http.server import HTTPServer, BaseHTTPRequestHandler
import json
from datetime import datetime
import time
import sys

class StreamingBackendHandler(BaseHTTPRequestHandler):
    
    def do_GET(self):
        path = self.path
        
        # Parse size parameter for large responses
        size_bytes = 0
        chunk_size = 8192
        delay_ms = 0
        
        if '?' in path:
            from urllib.parse import parse_qs, urlparse
            params = parse_qs(urlparse(path).query)
            size_bytes = int(params.get('size', [0])[0])
            chunk_size = int(params.get('chunk', [chunk_size])[0])
            delay_ms = int(params.get('delay', [0])[0])
        
        # Different endpoints for different tests
        if '/streaming' in path:
            self.handle_streaming_response(size_bytes, chunk_size, delay_ms)
        elif '/large' in path:
            self.handle_large_response(size_bytes or 5242880)  # Default 5MB
        elif '/slow' in path:
            self.handle_slow_response(size_bytes or 102400, delay_ms or 100)
        elif '/burst' in path:
            self.handle_burst_response()
        elif '/incremental' in path:
            self.handle_incremental_response()
        else:
            self.handle_normal_response()
    
    def handle_normal_response(self):
        """Normal small response"""
        response_data = {
            "path": self.path,
            "timestamp": datetime.now().isoformat(),
            "type": "normal",
            "message": "Normal response for cache testing"
        }
        
        body = json.dumps(response_data, indent=2).encode()
        
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'max-age=3600')
        self.end_headers()
        
        self.wfile.write(body)
        self.log_request("Normal response", len(body))
    
    def handle_streaming_response(self, size_bytes, chunk_size, delay_ms):
        """Stream response in chunks with optional delay"""
        if size_bytes == 0:
            size_bytes = 1048576  # Default 1MB
        
        self.send_response(200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(size_bytes))
        self.send_header('Cache-Control', 'max-age=3600')
        self.send_header('X-Streaming', 'true')
        self.send_header('X-Chunk-Size', str(chunk_size))
        self.send_header('X-Delay-Ms', str(delay_ms))
        self.end_headers()
        
        bytes_sent = 0
        chunk_data = b'X' * chunk_size
        
        start_time = time.time()
        while bytes_sent < size_bytes:
            remaining = size_bytes - bytes_sent
            current_chunk = min(chunk_size, remaining)
            
            self.wfile.write(chunk_data[:current_chunk])
            self.wfile.flush()
            bytes_sent += current_chunk
            
            if delay_ms > 0:
                time.sleep(delay_ms / 1000.0)
        
        duration = time.time() - start_time
        self.log_request(f"Streaming response {size_bytes} bytes in {duration:.2f}s", size_bytes)
    
    def handle_large_response(self, size_bytes):
        """Send large response all at once"""
        self.send_response(200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(size_bytes))
        self.send_header('Cache-Control', 'max-age=3600')
        self.send_header('X-Response-Size', str(size_bytes))
        self.end_headers()
        
        # Send in larger chunks for efficiency
        chunk_size = 65536  # 64KB chunks
        bytes_sent = 0
        chunk_data = b'L' * chunk_size
        
        while bytes_sent < size_bytes:
            remaining = size_bytes - bytes_sent
            current_chunk = min(chunk_size, remaining)
            self.wfile.write(chunk_data[:current_chunk])
            bytes_sent += current_chunk
        
        self.log_request(f"Large response", size_bytes)
    
    def handle_slow_response(self, size_bytes, delay_ms):
        """Slow drip response to test buffering"""
        chunk_size = 1024  # 1KB chunks slowly
        
        self.send_response(200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(size_bytes))
        self.send_header('Cache-Control', 'max-age=3600')
        self.send_header('X-Slow-Response', 'true')
        self.end_headers()
        
        bytes_sent = 0
        chunk_data = b'S' * chunk_size
        
        while bytes_sent < size_bytes:
            remaining = size_bytes - bytes_sent
            current_chunk = min(chunk_size, remaining)
            
            self.wfile.write(chunk_data[:current_chunk])
            self.wfile.flush()
            bytes_sent += current_chunk
            
            # Delay between chunks
            time.sleep(delay_ms / 1000.0)
        
        self.log_request(f"Slow response {delay_ms}ms delay per KB", size_bytes)
    
    def handle_burst_response(self):
        """Send data in bursts to test buffer watermarking"""
        total_size = 2097152  # 2MB
        burst_size = 262144   # 256KB bursts
        pause_ms = 200        # 200ms pause between bursts
        
        self.send_response(200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(total_size))
        self.send_header('Cache-Control', 'max-age=3600')
        self.send_header('X-Burst-Pattern', 'true')
        self.end_headers()
        
        bytes_sent = 0
        burst_data = b'B' * burst_size
        burst_count = 0
        
        while bytes_sent < total_size:
            remaining = total_size - bytes_sent
            current_burst = min(burst_size, remaining)
            
            # Send burst
            self.wfile.write(burst_data[:current_burst])
            self.wfile.flush()
            bytes_sent += current_burst
            burst_count += 1
            
            # Pause between bursts
            if bytes_sent < total_size:
                time.sleep(pause_ms / 1000.0)
        
        self.log_request(f"Burst response {burst_count} bursts", total_size)
    
    def handle_incremental_response(self):
        """Incrementally increase chunk sizes"""
        self.send_response(200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Transfer-Encoding', 'chunked')
        self.send_header('Cache-Control', 'max-age=3600')
        self.send_header('X-Incremental', 'true')
        self.end_headers()
        
        # Start small, increase chunk size
        sizes = [1024, 2048, 4096, 8192, 16384, 32768, 65536]
        total_sent = 0
        
        for size in sizes:
            chunk = b'I' * size
            # Send chunk in HTTP chunked encoding format
            chunk_header = f"{size:X}\r\n".encode()
            self.wfile.write(chunk_header)
            self.wfile.write(chunk)
            self.wfile.write(b"\r\n")
            self.wfile.flush()
            total_sent += size
            time.sleep(0.1)
        
        # Send final chunk
        self.wfile.write(b"0\r\n\r\n")
        
        self.log_request(f"Incremental response", total_sent)
    
    def do_POST(self):
        """Handle POST for upload buffering tests"""
        content_length = int(self.headers.get('Content-Length', 0))
        
        # Read data in chunks to test upload buffering
        chunk_size = 8192
        bytes_read = 0
        
        start_time = time.time()
        while bytes_read < content_length:
            remaining = content_length - bytes_read
            to_read = min(chunk_size, remaining)
            chunk = self.rfile.read(to_read)
            bytes_read += len(chunk)
        
        duration = time.time() - start_time
        
        response_data = {
            "uploaded_bytes": content_length,
            "duration_seconds": round(duration, 3),
            "throughput_mbps": round((content_length / duration) / 1048576, 2) if duration > 0 else 0
        }
        
        body = json.dumps(response_data, indent=2).encode()
        
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        
        self.wfile.write(body)
        
        self.log_request(f"POST upload {content_length} bytes in {duration:.2f}s", content_length)
    
    def log_request(self, message, size=0):
        timestamp = datetime.now().strftime('%H:%M:%S')
        size_str = f" ({self.format_bytes(size)})" if size > 0 else ""
        print(f"[{timestamp}] {self.command} {self.path} - {message}{size_str}")
    
    @staticmethod
    def format_bytes(bytes):
        """Format bytes for human reading"""
        for unit in ['B', 'KB', 'MB', 'GB']:
            if bytes < 1024.0:
                return f"{bytes:.1f}{unit}"
            bytes /= 1024.0
        return f"{bytes:.1f}TB"
    
    def log_message(self, format, *args):
        # Suppress default logging
        pass

def run_server(port=3000):
    server_address = ('', port)
    httpd = HTTPServer(server_address, StreamingBackendHandler)
    
    print("=" * 70)
    print("Streaming Backend Server for Buffer Watermarking Tests")
    print("=" * 70)
    print(f"Listening on: http://localhost:{port}")
    print()
    print("Available endpoints:")
    print(f"  /normal                - Normal small response")
    print(f"  /streaming?size=N      - Stream N bytes (default 1MB)")
    print(f"  /streaming?size=N&chunk=C&delay=D")
    print(f"                         - Stream with chunk size C and delay D ms")
    print(f"  /large?size=N          - Large response N bytes (default 5MB)")
    print(f"  /slow?size=N&delay=D   - Slow drip, D ms delay per KB")
    print(f"  /burst                 - Bursty 2MB response")
    print(f"  /incremental           - Incrementally increasing chunks")
    print()
    print("Examples:")
    print(f"  curl http://localhost:{port}/streaming?size=2097152")
    print(f"  curl http://localhost:{port}/slow?size=1048576&delay=50")
    print(f"  curl -X POST --data-binary @large-file.bin http://localhost:{port}/upload")
    print("=" * 70)
    print()
    
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down server...")
        httpd.shutdown()

if __name__ == '__main__':
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
    run_server(port)