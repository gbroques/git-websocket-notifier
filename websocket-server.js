#!/usr/bin/env node
import { WebSocketServer } from 'ws';

if (process.argv.length < 3) {
    console.log('Usage: websocket-server.js <port>')
    process.exit(1);
}
const websocketServer = new WebSocketServer({ port: process.argv[2] });

websocketServer.on('connection', function connection(websocket) {
  websocket.on('message', function incoming(message) {
    console.log('received: %s', message);
  });
});
