/* ============================================================
 * 个人主页交互脚本
 * 1. 粒子网络背景（跟随鼠标）
 * 2. 打字机 / 滚动入场 / 技能条动画
 * 3. 作品的加载 / 添加 / 删除（对接 C++ 后端 API）
 * ============================================================ */
(function () {
    'use strict';

    /* ---------------- 粒子背景 ---------------- */
    const canvas = document.getElementById('bgCanvas');
    const ctx = canvas.getContext('2d');
    let particles = [];
    const mouse = { x: null, y: null, radius: 150 };

    function resizeCanvas() {
        canvas.width = window.innerWidth;
        canvas.height = window.innerHeight;
        const count = Math.min(110, Math.floor((canvas.width * canvas.height) / 16000));
        particles = Array.from({ length: count }, () => ({
            x: Math.random() * canvas.width,
            y: Math.random() * canvas.height,
            vx: (Math.random() - 0.5) * 0.55,
            vy: (Math.random() - 0.5) * 0.55,
            r: Math.random() * 1.7 + 0.6
        }));
    }

    function drawParticles() {
        ctx.clearRect(0, 0, canvas.width, canvas.height);

        for (const p of particles) {
            p.x += p.vx;
            p.y += p.vy;
            if (p.x < 0 || p.x > canvas.width) p.vx *= -1;
            if (p.y < 0 || p.y > canvas.height) p.vy *= -1;

            ctx.beginPath();
            ctx.arc(p.x, p.y, p.r, 0, Math.PI * 2);
            ctx.fillStyle = 'rgba(34, 211, 238, 0.65)';
            ctx.fill();
        }

        // 粒子间连线 & 鼠标连线
        for (let i = 0; i < particles.length; i++) {
            const a = particles[i];
            for (let j = i + 1; j < particles.length; j++) {
                const b = particles[j];
                const dist = Math.hypot(a.x - b.x, a.y - b.y);
                if (dist < 120) {
                    ctx.beginPath();
                    ctx.moveTo(a.x, a.y);
                    ctx.lineTo(b.x, b.y);
                    ctx.strokeStyle = 'rgba(167, 139, 250, ' + (0.16 * (1 - dist / 120)) + ')';
                    ctx.stroke();
                }
            }
            if (mouse.x !== null) {
                const dm = Math.hypot(a.x - mouse.x, a.y - mouse.y);
                if (dm < mouse.radius) {
                    ctx.beginPath();
                    ctx.moveTo(a.x, a.y);
                    ctx.lineTo(mouse.x, mouse.y);
                    ctx.strokeStyle = 'rgba(34, 211, 238, ' + (0.35 * (1 - dm / mouse.radius)) + ')';
                    ctx.stroke();
                }
            }
        }
        requestAnimationFrame(drawParticles);
    }

    window.addEventListener('resize', resizeCanvas);
    window.addEventListener('mousemove', (e) => { mouse.x = e.clientX; mouse.y = e.clientY; });
    window.addEventListener('mouseout', () => { mouse.x = null; mouse.y = null; });
    resizeCanvas();
    drawParticles();

    /* ---------------- 打字机效果 ---------------- */
    const phrases = ['C++ 开发者', 'STL 实践者', '算法爱好者', '终身学习者', 'Go 语言探索者'];
    const typingEl = document.getElementById('typingText');
    let pi = 0, ci = 0, deleting = false;

    function typeLoop() {
        const word = phrases[pi];
        typingEl.textContent = word.slice(0, ci);
        let delay = deleting ? 60 : 130;

        if (!deleting && ci === word.length) {
            delay = 1600;
            deleting = true;
        } else if (deleting && ci === 0) {
            deleting = false;
            pi = (pi + 1) % phrases.length;
            delay = 350;
        } else {
            ci += deleting ? -1 : 1;
        }
        setTimeout(typeLoop, delay);
    }
    typeLoop();

    /* ---------------- 导航栏 & 移动端菜单 ---------------- */
    const navbar = document.getElementById('navbar');
    const navLinks = document.querySelectorAll('.nav-link');
    const navToggle = document.getElementById('navToggle');
    const linksBox = document.querySelector('.nav-links');

    window.addEventListener('scroll', () => {
        navbar.classList.toggle('scrolled', window.scrollY > 40);

        // 高亮当前区域
        const sections = document.querySelectorAll('section, .hero');
        let current = '';
        sections.forEach((sec) => {
            if (window.scrollY >= sec.offsetTop - 160) current = sec.id;
        });
        navLinks.forEach((a) => {
            a.classList.toggle('active', a.getAttribute('href') === '#' + current);
        });
    });

    navToggle.addEventListener('click', () => linksBox.classList.toggle('open'));
    navLinks.forEach((a) => a.addEventListener('click', () => linksBox.classList.remove('open')));

    /* ---------------- 滚动入场动画 ---------------- */
    const revealObserver = new IntersectionObserver((entries) => {
        entries.forEach((entry) => {
            if (entry.isIntersecting) {
                entry.target.classList.add('visible');
                revealObserver.unobserve(entry.target);
            }
        });
    }, { threshold: 0.12 });
    document.querySelectorAll('.reveal').forEach((el) => revealObserver.observe(el));

    /* ---------------- 技能条动画 ---------------- */
    const skillObserver = new IntersectionObserver((entries) => {
        entries.forEach((entry) => {
            if (entry.isIntersecting) {
                const fill = entry.target;
                fill.style.width = fill.dataset.level + '%';
                skillObserver.unobserve(fill);
            }
        });
    }, { threshold: 0.5 });
    document.querySelectorAll('.skill-fill').forEach((el) => skillObserver.observe(el));

    /* ---------------- Toast 提示 ---------------- */
    const toastWrap = document.getElementById('toastWrap');
    function toast(message, type) {
        const el = document.createElement('div');
        el.className = 'toast ' + (type || '');
        el.textContent = message;
        toastWrap.appendChild(el);
        setTimeout(() => {
            el.classList.add('hide');
            setTimeout(() => el.remove(), 320);
        }, 2600);
    }

    /* ---------------- 作品管理 ---------------- */
    const grid = document.getElementById('projectsGrid');
    const modalMask = document.getElementById('modalMask');
    const form = document.getElementById('projectForm');
    const formError = document.getElementById('formError');

    function escapeHtml(s) {
        const div = document.createElement('div');
        div.textContent = s == null ? '' : String(s);
        return div.innerHTML;
    }

    function safeUrl(url) {
        return /^https?:\/\//i.test(url) ? url : '#';
    }

    function renderProjects(list) {
        if (!list.length) {
            grid.innerHTML = '<div class="empty-state"><span class="big">&#128640;</span>' +
                '还没有作品，点击右上角「添加作品」开始展示吧！</div>';
            return;
        }
        grid.innerHTML = list.map((p) => {
            const tags = (p.tags || [])
                .map((t) => '<span class="tag">' + escapeHtml(t) + '</span>')
                .join('');
            return '<article class="project-card reveal visible">' +
                '<h3>' + escapeHtml(p.title) + '</h3>' +
                '<p class="project-desc">' + escapeHtml(p.description || '暂无描述') + '</p>' +
                '<div class="project-tags">' + tags + '</div>' +
                '<div class="project-footer">' +
                '<a class="project-link" href="' + escapeHtml(safeUrl(p.url)) +
                    '" target="_blank" rel="noopener noreferrer">访问作品 &rarr;</a>' +
                '<button class="project-delete" data-id="' + p.id + '">&#128465; 删除</button>' +
                '</div></article>';
        }).join('');
    }

    async function loadProjects() {
        grid.innerHTML = '<div class="loading-state"><div class="spinner"></div>加载作品中…</div>';
        try {
            const res = await fetch('/api/projects');
            if (!res.ok) throw new Error('HTTP ' + res.status);
            renderProjects(await res.json());
        } catch (err) {
            grid.innerHTML = '<div class="empty-state"><span class="big">&#9888;&#65039;</span>' +
                '作品加载失败，请确认服务器正在运行</div>';
        }
    }

    function openModal() {
        form.reset();
        formError.textContent = '';
        modalMask.classList.add('show');
        form.querySelector('input[name="title"]').focus();
    }
    function closeModal() {
        modalMask.classList.remove('show');
    }

    document.getElementById('addProjectBtn').addEventListener('click', openModal);
    document.getElementById('modalClose').addEventListener('click', closeModal);
    document.getElementById('modalCancel').addEventListener('click', closeModal);
    modalMask.addEventListener('click', (e) => {
        if (e.target === modalMask) closeModal();
    });
    document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape') closeModal();
    });

    form.addEventListener('submit', async (e) => {
        e.preventDefault();
        formError.textContent = '';

        const data = new FormData(form);
        const title = (data.get('title') || '').toString().trim();
        const url = (data.get('url') || '').toString().trim();
        const description = (data.get('description') || '').toString().trim();
        const tags = (data.get('tags') || '').toString()
            .split(/[,，]/)
            .map((t) => t.trim())
            .filter(Boolean)
            .slice(0, 8);

        if (!title) { formError.textContent = '请填写作品标题'; return; }
        if (!/^https?:\/\/.+/i.test(url)) { formError.textContent = '链接必须以 http:// 或 https:// 开头'; return; }

        const submitBtn = form.querySelector('button[type="submit"]');
        submitBtn.disabled = true;
        try {
            const res = await fetch('/api/projects', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ title, url, description, tags })
            });
            const result = await res.json();
            if (!res.ok) throw new Error(result.error || '保存失败');
            toast('作品添加成功', 'success');
            closeModal();
            loadProjects();
        } catch (err) {
            formError.textContent = err.message;
        } finally {
            submitBtn.disabled = false;
        }
    });

    // 事件委托：删除作品
    grid.addEventListener('click', async (e) => {
        const btn = e.target.closest('.project-delete');
        if (!btn) return;
        if (!confirm('确定删除这条作品吗？')) return;
        try {
            const res = await fetch('/api/projects/' + btn.dataset.id, { method: 'DELETE' });
            if (!res.ok) throw new Error();
            toast('已删除', 'success');
            loadProjects();
        } catch (err) {
            toast('删除失败，请稍后重试', 'error');
        }
    });

    loadProjects();
})();
