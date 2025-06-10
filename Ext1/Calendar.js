console.log("JavaScript loaded");

const monthNames = [
    "Janeiro", "Fevereiro", "Março", "Abril", "Maio", "Junho",
    "Julho", "Agosto", "Setembro", "Outubro", "Novembro", "Dezembro"
];

let today = new Date();
let currentMonth = today.getMonth();
let currentYear = today.getFullYear();

function setCookie(name, value, days = 7) {
    let expires = "";
    if (days) {
        const date = new Date();
        date.setTime(date.getTime() + (days*24*60*60*1000));
        expires = "; expires=" + date.toUTCString();
    }
    document.cookie = name + "=" + (value || "")  + expires + "; path=/";
}

function getCookie(name) {
    const nameEQ = name + "=";
    const ca = document.cookie.split(';');
    for(let i=0;i < ca.length;i++) {
        let c = ca[i];
        while (c.charAt(0)==' ') c = c.substring(1,c.length);
        if (c.indexOf(nameEQ) == 0) return c.substring(nameEQ.length,c.length);
    }
    return null;
}

function getToken() {
    let token = getCookie('userToken');
    if (!token) {
        token = Math.random().toString(36).slice(2, 16);
        setCookie('userToken', token, 7);
    }
    return token;
}

function renderCalendar(month, year) {
    const monthYearDisplay = document.getElementById('month-year-display');
    const daysContainer = document.querySelector('.number-days');
    daysContainer.innerHTML = '';

    let firstDay = new Date(year, month, 1).getDay();

    let daysInMonth = new Date(year, month + 1, 0).getDate();

    let daysInPrevMonth = new Date(year, month, 0).getDate();

    for (let i = firstDay; i > 0; i--) {
        daysContainer.innerHTML += `<span class="mes-anterior">${daysInPrevMonth - i + 1}</span>`;
    }

    for (let i = 1; i <= daysInMonth; i++) {
        daysContainer.innerHTML += `<span class="day" data-day="${i}" data-month="${month}" data-year="${year}">${i}</span>`;
    }

    monthYearDisplay.textContent = `${monthNames[month]} / ${year}`;
}

function changeMonth(delta) {
    currentMonth += delta;

    if (currentMonth < 0) {
        currentMonth = 11;
        currentYear--;
    } else if (currentMonth > 11) {
        currentMonth = 0;
        currentYear++;
    }

    document.getElementById("yearInput").value = currentYear;
    renderCalendar(currentMonth, currentYear);

    highlightDay();
}

function changeYear() {
    let yearInput = document.getElementById("yearInput").value.trim();
    let year = parseInt(yearInput, 10);

    if (isNaN(year) || year < 1970 || year > 2100) {
        alert("Ano inválido. Use um valor entre 1970 e 2100.");
        document.getElementById("yearInput").value = currentYear;
        return;
    }

    currentYear = year;
    renderCalendar(currentMonth, currentYear);
}

function parseDateLocal(str) {
    const [year, month, day] = str.split("-").map(Number);
    return new Date(year, month - 1, day);
}

function compareYMD(d1, d2) {
    return d1.getFullYear() === d2.getFullYear()
        && d1.getMonth() === d2.getMonth()
        && d1.getDate() === d2.getDate();
}

function highlightDay() {
    const startInput = document.getElementById("dataInicio").value;
    const endInput = document.getElementById("dataFim").value;

    const startDate = parseDateLocal(startInput);
    const endDate = parseDateLocal(endInput);

    const diffDays = Math.floor((endDate - startDate) / (1000 * 60 * 60 * 24));
    const midDate = new Date(startDate);
    midDate.setDate(midDate.getDate() + Math.floor(diffDays / 2));

    document.querySelectorAll('.day').forEach(el => {
        el.classList.remove('destacado-inicio', 'destacado-meio', 'destacado-fim');
    });

    document.querySelectorAll('.day').forEach(el => {
        const day = parseInt(el.getAttribute('data-day'));
        const month = parseInt(el.getAttribute('data-month'));
        const year = parseInt(el.getAttribute('data-year'));

        const elDate = new Date(year, month, day);

        if (compareYMD(elDate, startDate)) {
            el.classList.add('destacado-inicio');
        } else if (compareYMD(elDate, endDate)) {
            el.classList.add('destacado-fim');
        } else if (elDate > startDate && elDate < endDate) {
            el.classList.add('destacado-meio');
        }  
    });
}

function salvarCompromisso() {
    const token = getToken();
    const importanciaValor = document.getElementById("afinidade").value;
    const importanciaTextoValor = importanciaTexto(importanciaValor);
    const afazeres = document.getElementById("afazeres").value.trim();
    const dataInicioStr = document.getElementById("dataInicio").value.trim();
    const dataFimStr = document.getElementById("dataFim").value.trim();
    const horaInicio = document.getElementById("horaInicio").value.trim();
    const horaFim = document.getElementById("horaFim").value.trim();

    if (!dataInicioStr || !dataFimStr || !horaInicio || !horaFim || !afazeres) {
        alert("Preencha todos os campos antes de salvar.");
        return;
    }

    const dataInicio = new Date(dataInicioStr);
    const dataFim = new Date(dataFimStr);

    if (isNaN(dataInicio.getTime()) || isNaN(dataFim.getTime())) {
        alert("Datas inválidas.");
        return;
    }

    if (dataFim < dataInicio) {
        alert("A data final deve ser igual ou posterior à data inicial.");
        return;
    }

    let compromissos = JSON.parse('[]');

    compromissos.push({
        afazeres: afazeres,
        dataInicio: dataInicioStr,
        dataFim: dataFimStr,
        horaInicio: horaInicio,
        horaFim: horaFim,
        importancia: importanciaTextoValor
    });

    compromissos = compromissos.flat();
    
    if (!compromissos) {
        alert("No data to send to server.");
        return;
    }

    fetch("/save?token=" + token, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(compromissos)
    })
    .then(res => {
        if (!res.ok) throw new Error("Failed to save on server");
        return res.text();
    })
    .catch(err => alert("Failed: " + err.message));

    importarDoServidor()
    
    document.getElementById("afazeres").value = '';
    document.getElementById("dataInicio").value = '';
    document.getElementById("dataFim").value = '';
    document.getElementById("horaInicio").value = '';
    document.getElementById("horaFim").value = '';
    document.getElementById("afinidade").value = '';
}

function renderTabela(commitments) {
    const tbody = document.querySelector("#chart tbody");
    tbody.innerHTML = '';

    const importanciaMap = { "Nenhuma": 0, "Pequena": 5, "Média": 10, "Grande": 20, "Extrema": 50, "Exclusiva": 60 };

    commitments.sort((a, b) => {
        const impA = importanciaMap[a.importancia] || 0;
        const impB = importanciaMap[b.importancia] || 0;

        if (impB !== impA) return impB - impA;

        const dataA = new Date(a.dataInicio);
        const dataB = new Date(b.dataInicio);
        return dataA - dataB;
    });

    commitments.forEach(item => {
        const tr = document.createElement("tr");
        let classeImportancia = item.importancia.toLowerCase();
        if (!["nenhuma","pequena","média","media","grande","extrema","exclusiva"].includes(classeImportancia)) {
            classeImportancia = "nenhuma";
        }
        tr.classList.add(classeImportancia);

        tr.innerHTML = `
            <td>${item.afazeres}</td>
            <td>${item.dataInicio} → ${item.dataFim}</td>
            <td>${item.horaInicio} → ${item.horaFim}</td>
            <td>${item.importancia}</td>
        `;
        tbody.appendChild(tr);
    });
}

function salvarTudo() {
    highlightDay();
    salvarCompromisso();
}

function exportarJSON() {
    const token = getToken();
    const url = `/load?token=${token}`;

    fetch(url)
        .then(response => {
            if (!response.ok) {
                throw new Error("Erro ao buscar dados do servidor. Código: " + response.status);
            }
            return response.json();
        })
        .then(dados => {
            if (!Array.isArray(dados)) {
                throw new Error("Dados do servidor não estão em formato de lista.");
            }

            const blob = new Blob([JSON.stringify(dados, null, 2)], { type: "application/json" });
            const downloadUrl = URL.createObjectURL(blob);
            const a = document.createElement("a");

            a.href = downloadUrl;
            a.download = `user_${token}.json`;
            document.body.appendChild(a);
            a.click();

            document.body.removeChild(a);
            URL.revokeObjectURL(downloadUrl);

            alert("Compromissos exportados com sucesso.");
        })
        .catch(err => {
            alert("Erro ao exportar JSON: " + err.message);
        });
}

function importarDoServidor() {
    const token = getToken();

    fetch(`/load?token=${token}`)
        .then(res => {
            if (!res.ok) throw new Error("Failed to load data from server");
            return res.json();
        })
        .then(data => {
            if (!Array.isArray(data)) throw new Error("Invalid data format");
            renderTabela(data);
        })
        .catch(err => {
            console.warn("Could not load data from server:", err.message);
        });
}

function importarJSON(event) {
    const file = event.target.files[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = function(e) {
        try {
            const token = getToken();
            const dados = JSON.parse(e.target.result);
            if (!Array.isArray(dados)) throw new Error("Formato inválido");

            localStorage.setItem("user_" + token, JSON.stringify(dados));
            renderTabela(dados);
            alert("Compromissos importados com sucesso.");
        } catch (err) {
            alert("Erro ao importar JSON: " + err.message);
        }
    };
    reader.readAsText(file);
}

function importanciaTexto(valor) {
    switch(valor) {
        case "0": return "Nenhuma";
        case "5": return "Pequena";
        case "10": return "Média";
        case "20": return "Grande";
        case "50": return "Extrema";
        default: return "Desconhecida";
    }
}

window.onload = () => {

    document.getElementById("yearInput").value = currentYear;
    renderCalendar(currentMonth, currentYear);
    highlightDay();

    importarDoServidor();

    document.getElementById("dataInicio").addEventListener('change', highlightDay);
    document.getElementById("dataFim").addEventListener('change', highlightDay);
};