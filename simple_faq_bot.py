from flask import Flask, request, jsonify

app = Flask(__name__)

FAQS = {
    "how to run": "FAQ Bot: To run this project:\n1. gcc server.c -o server -lpthread -lcurl -ljson-c\n2. gcc client.c -o client -lpthread\n3. ./server\n4. ./client 127.0.0.1",
    "difficulty": "FAQ Bot: Difficulty: Intermediate C programming. Needs: sockets, threading, file I/O knowledge.",
    "features": "FAQ Bot: Features: Multi-threading, authentication, private messages, file transfer, 50 concurrent users.",
    "compile": "FAQ Bot: Compilation: Install build-essential, libcurl4-openssl-dev, libjson-c-dev libraries.",
    "error": "FAQ Bot: Common errors: Check libraries, port 8080 availability, file permissions."
}

@app.route('/faq', methods=['POST'])
def faq():
    try:
        data = request.get_json()
        question = data.get('question', '').lower()
        
        for keyword, answer in FAQS.items():
            if keyword in question:
                return jsonify({'answer': answer})
        
        return jsonify({'answer': 'FAQ Bot: Try asking about: how to run, difficulty, features, compile, error'})
        
    except Exception as e:
        return jsonify({'answer': f'FAQ Bot: Error - {str(e)}'})

if __name__ == '__main__':
    print("Starting Simple FAQ Bot on port 5005...")
    app.run(host='0.0.0.0', port=5005)
