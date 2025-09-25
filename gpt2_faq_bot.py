from flask import Flask, request, jsonify
from transformers import GPT2LMHeadModel, GPT2Tokenizer
import torch
import re

app = Flask(__name__)

print("Loading GPT-2 model...")
tokenizer = GPT2Tokenizer.from_pretrained("gpt2")
model = GPT2LMHeadModel.from_pretrained("gpt2")
tokenizer.pad_token = tokenizer.eos_token
print("GPT-2 model loaded successfully!")

# PROJECT KNOWLEDGE (for project-specific questions)
PROJECT_KNOWLEDGE = {
    "how to run": "FAQ Bot: To run this chat server project:\n1. gcc server.c -o server -lpthread -lcurl -ljson-c\n2. gcc client.c -o client -lpthread\n3. ./server (start server)\n4. ./client 127.0.0.1 (connect client)",
    "compile": "FAQ Bot: Compilation steps:\n• Install: sudo apt install libcurl4-openssl-dev libjson-c-dev\n• Server: gcc server.c -o server -lpthread -lcurl -ljson-c\n• Client: gcc client.c -o client -lpthread",
    "features": "FAQ Bot: Project Features:\n• Multi-threading (50 concurrent users)\n• User authentication (register/login)\n• Private messaging (/msg username)\n• File transfer (put/get commands)\n• Memory efficient (7KB per client)\n• AI-powered FAQ bot (that's me!)",
    "difficulty": "FAQ Bot: Project Difficulty:\n• Level: Intermediate to Advanced\n• Skills needed: C programming, socket programming, multi-threading",
    "commands": "FAQ Bot: Available Commands:\n• /login, /register, /msg, /users, /faq, put, get"
}

def get_project_response(question):
    """Check if question is about the project"""
    question_lower = question.lower().strip()
    
    for keyword, response in PROJECT_KNOWLEDGE.items():
        if keyword in question_lower:
            return response
    
    # Check for project keywords
    project_keywords = ["server", "client", "socket", "thread", "chat", "compile", "gcc", "authentication"]
    if any(keyword in question_lower for keyword in project_keywords):
        return "FAQ Bot: Ask me about: 'how to run', 'features', 'commands', 'difficulty', or 'compile'"
    
    return None

def generate_real_gpt2_response(question):
    """Generate ACTUAL GPT-2 responses with better prompting"""
    try:
        print(f"🤖 Using REAL GPT-2 for: {question}")
        
        # Better prompt engineering for different question types
        if "what is" in question.lower():
            prompt = f"Q: {question}\nA: {question.split('what is')[-1].strip().title()} is"
        elif "explain" in question.lower():
            prompt = f"Question: {question}\nExplanation:"
        elif "how are you" in question.lower():
            prompt = "Human: How are you?\nAI: I am"
        elif "tell me" in question.lower():
            prompt = f"Human: {question}\nAI:"
        elif "joke" in question.lower():
            prompt = "Human: Tell me a joke\nAI: Here's a joke:"
        elif "name" in question.lower():
            prompt = "Human: What is your name?\nAI: My name is"
        else:
            prompt = f"Human: {question}\nAI:"
        
        # Tokenize input
        inputs = tokenizer.encode(prompt, return_tensors="pt", max_length=100, truncation=True)
        
        # Generate with GPT-2
        with torch.no_grad():
            outputs = model.generate(
                inputs,
                max_length=inputs.shape[1] + 30,  # Generate 30 new tokens
                num_return_sequences=1,
                temperature=0.8,  # Control randomness
                top_k=50,        # Limit vocabulary
                top_p=0.95,      # Nucleus sampling
                do_sample=True,
                pad_token_id=tokenizer.eos_token_id,
                repetition_penalty=1.2,  # Reduce repetition
                no_repeat_ngram_size=2   # Avoid repeating phrases
            )
        
        # Decode the response
        full_response = tokenizer.decode(outputs[0], skip_special_tokens=True)
        
        # Extract only the generated part
        generated_text = full_response[len(prompt):].strip()
        
        print(f"🔥 Raw GPT-2 output: {generated_text}")
        
        # Clean up the response
        if generated_text:
            # Take the first complete sentence
            sentences = re.split(r'[.!?]+', generated_text)
            if sentences:
                clean_response = sentences[0].strip()
                
                # Remove unwanted patterns
                clean_response = re.sub(r'(Human|AI|Assistant|User):', '', clean_response)
                clean_response = re.sub(r'\n.*', '', clean_response, flags=re.DOTALL)
                clean_response = clean_response.strip()
                
                # Ensure it's reasonable length and content
                if 5 < len(clean_response) < 200:
                    # Add proper ending if missing
                    if not clean_response.endswith(('.', '!', '?')):
                        clean_response += '.'
                    
                    return f"GPT-2 Bot: {clean_response}"
        
        # Fallback if GPT-2 output is too weird
        return "GPT-2 Bot: I'm still learning to answer that properly. Try asking something simpler!"
        
    except Exception as e:
        print(f"❌ GPT-2 Error: {e}")
        return "GPT-2 Bot: I'm having trouble processing that question right now."

@app.route('/faq', methods=['POST'])
def faq():
    try:
        data = request.get_json()
        question = data.get('question', '').strip()
        
        if not question:
            return jsonify({'answer': 'FAQ Bot: Please ask a question!'})
        
        print(f"📥 Question received: {question}")
        
        # STEP 1: Check if it's about the project
        project_answer = get_project_response(question)
        if project_answer:
            print("✅ Using PROJECT knowledge")
            return jsonify({'answer': project_answer})
        
        # STEP 2: Use REAL GPT-2 for general questions
        print("🤖 Using REAL GPT-2 model...")
        gpt2_answer = generate_real_gpt2_response(question)
        return jsonify({'answer': gpt2_answer})
        
    except Exception as e:
        return jsonify({'answer': f'FAQ Bot: Error - {str(e)}'})

if __name__ == '__main__':
    print("🚀 Starting REAL GPT-2 FAQ Bot")
    print("📋 Project questions → Predefined answers")
    print("🤖 General questions → ACTUAL GPT-2 generation")
    app.run(host='0.0.0.0', port=5005, debug=False)
