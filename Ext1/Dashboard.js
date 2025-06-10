function generateToken(length = 24) {
  const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
  let token = '';
  for (let i = 0; i < length; i++) {
    token += chars.charAt(Math.floor(Math.random() * chars.length));
  }
  return token;
}

function setCookie(name, value, days = 7) {
    let expires = "";
    if (days) {
        const date = new Date();
        date.setTime(date.getTime() + (days*24*60*60*1000));
        expires = "; expires=" + date.toUTCString();
    }
    document.cookie = name + "=" + (value || "")  + expires + "; path=/";
}

function login(event) {
    event.preventDefault();

    const user = document.getElementById("username").value;
    const pass = document.getElementById("password").value;

    fetch("/login", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ username: user, password: pass }),
        credentials: "include"
    })
    .then(res => {
        if (!res.ok) throw new Error("Invalid login credetials");
        return res.text();
    })
    .then(token => {
        setCookie('userToken', token, 7);
        window.location.href = "/Calendar.html";
    })
    .catch(err => {
        alert("Login failed: " + err.message);
    });

    return false;
}
