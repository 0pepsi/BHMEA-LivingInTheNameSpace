import socket
import threading
import time
from datetime import datetime
import sys

class C2Server:
    def __init__(self, host='127.0.0.1', port=8443):
        self.host = host
        self.port = port
        self.beacon_count = 0
        self.clients = {}
        self.running = True

    def handle_client(self, conn, addr):
        """Handle incoming beacon from persistence agent"""
        try:
            data = conn.recv(2048).decode('utf-8', errors='ignore')
            timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')

            # Parse HTTP request
            lines = data.split('\r\n')
            if lines:
                request_line = lines[0]

                # Track beacons
                self.beacon_count += 1

                # Extract endpoint
                parts = request_line.split()
                if len(parts) >= 2:
                    endpoint = parts[1]

                    # Colorful output
                    print(f"\n{'='*60}")
                    print(f"[{timestamp}] Beacon #{self.beacon_count}")
                    print(f"{'='*60}")
                    print(f"Source: {addr[0]}:{addr[1]}")
                    print(f"Endpoint: {endpoint}")

                    # Show if it's a status report
                    if '/status/' in endpoint:
                        status = endpoint.split('/')[-1]
                        print(f"Status Report: {status}")

                    print(f"{'='*60}\n")

                    # Log full request for debugging
                    if '--verbose' in sys.argv:
                        print("Full Request:")
                        print(data)
                        print()

            # Send HTTP 200 response
            response = (
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 2\r\n"
                "Connection: close\r\n"
                "\r\n"
                "OK"
            )
            conn.sendall(response.encode())

        except Exception as e:
            print(f"[ERROR] {e}")
        finally:
            conn.close()

    def start(self):
        """Start the C2 server"""
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

        try:
            server.bind((self.host, self.port))
            server.listen(10)

            print("╔" + "="*58 + "╗")
            print("║" + " "*58 + "║")
            print("║" + "  C2 Server - Linux Namespace Persistence Demo".center(58) + "║")
            print("║" + "  Black Hat MEA 2025".center(58) + "║")
            print("║" + " "*58 + "║")
            print("╚" + "="*58 + "╝")
            print()
            print(f"[*] Listening on {self.host}:{self.port}")
            print(f"[*] Waiting for beacons from persistence agents...")
            print(f"[*] Press Ctrl+C to stop\n")

            while self.running:
                try:
                    server.settimeout(1.0)
                    conn, addr = server.accept()
                    # Handle each connection in a new thread
                    threading.Thread(
                        target=self.handle_client,
                        args=(conn, addr),
                        daemon=True
                    ).start()
                except socket.timeout:
                    continue
                except KeyboardInterrupt:
                    break

        except Exception as e:
            print(f"[ERROR] Failed to start server: {e}")
            return 1
        finally:
            print("\n[*] Shutting down C2 server...")
            print(f"[*] Total beacons received: {self.beacon_count}")
            server.close()

        return 0

def main():
    """Main entry point"""
    print()

    # Check for help
    if '--help' in sys.argv or '-h' in sys.argv:
        print("Usage: python3 c2_server.py [--verbose]")
        print()
        print("Options:")
        print("  --verbose    Show full HTTP requests")
        print("  --help       Show this help message")
        print()
        return 0

    # Start server
    server = C2Server()
    try:
        return server.start()
    except KeyboardInterrupt:
        print("\n[*] Interrupted by user")
        return 0

if __name__ == '__main__':
    sys.exit(main())
